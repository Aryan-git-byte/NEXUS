"""Exercise real central framing and request logic with a simulated ATT peer."""
import asyncio
from types import SimpleNamespace
from unittest.mock import Mock

from companion.bluetooth import BluetoothManager
from companion.profiles import ProfileManager
from companion.protocol import (PROFILE_UUID, RESPONSE_UUID, STATUS_UUID, Reassembler,
                                decode_json, encode_json, fragment)
from companion.settings import Settings


def manager():
    return BluetoothManager(Settings(auto_connect=False), Mock(), Mock(), Mock(), Mock())


def test_ack_retry_preserves_sequence_and_reassembles_at_minimum_mtu():
    async def scenario():
        central = manager()
        central.session = "a" * 32
        received = []
        assemblers = {STATUS_UUID: Reassembler()}

        class Peer:
            is_connected = True
            mtu_size = 23

            async def write_gatt_char(self, channel, packet, response):
                assert response and len(packet) <= 20
                payload = assemblers[channel].feed(packet)
                if payload:
                    message = decode_json(payload)
                    received.append(message)
                    if len(received) == 2:
                        reply = {"type": "hello_ack", "sequence": message["sequence"],
                                 "session": central.session, "status": "success", "message": "Accepted"}
                        for part in fragment(encode_json(reply), 82, 20):
                            central._notify(SimpleNamespace(uuid=STATUS_UUID), bytearray(part))

        central.client = Peer()
        hello = {"type": "hello", "sequence": 21, "session": central.session,
                 "protocol": 1, "pc_name": "Test peer", "epoch": 1793952000, "utc_offset": 19800}
        reply = await central._request(STATUS_UUID, hello, "hello_ack", timeout=0.01)
        assert reply["status"] == "success"
        assert received == [hello, hello]
        assert not central.pending
    asyncio.run(scenario())


def test_reply_can_interleave_a_large_profile_write():
    async def scenario():
        central = manager()
        writes = []
        first_packet = asyncio.Event()

        class Peer:
            is_connected = True
            mtu_size = 23

            async def write_gatt_char(self, channel, packet, response):
                writes.append(channel)
                first_packet.set()
                await asyncio.sleep(0)

        central.client = Peer()
        profile = asyncio.create_task(central._write(PROFILE_UUID, {"type": "profile_sync", "data": "x" * 1000}))
        await first_packet.wait()
        await central._write(RESPONSE_UUID, {"type": "response", "status": "success"})
        await profile
        assert writes.index(RESPONSE_UUID) < len(writes) - 1
        assert PROFILE_UUID in writes[writes.index(RESPONSE_UUID) + 1:]
    asyncio.run(scenario())


def test_saved_edits_during_sync_remain_the_next_reconnect_snapshot(tmp_path):
    async def scenario():
        profiles = ProfileManager(tmp_path).snapshot()
        central = manager()
        central.session = "a" * 32
        central.client = SimpleNamespace(is_connected=True)
        central.configure_profiles(profiles, "default")
        central.dispatcher.begin(central.session, profiles)
        newer = ProfileManager(tmp_path).snapshot()
        newer["development"]["buttons"][0]["label"] = "Updated offline"

        async def acknowledge(*args, **kwargs):
            central.configure_profiles(newer, "development")
            return {"type": "profile_ack", "status": "success"}

        central._request = acknowledge
        await central.synchronize(profiles, "default")
        assert central.saved_profiles == newer
        assert central.active == "development"
        assert central.dispatcher.profiles == profiles
    asyncio.run(scenario())
