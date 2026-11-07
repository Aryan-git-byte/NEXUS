"""Exercise real central framing and request logic with a simulated ATT peer."""
import asyncio
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock

import pytest
from companion import bluetooth
from companion.protocol import SERVICE_UUID

from companion.bluetooth import BluetoothManager
from companion.profiles import ProfileManager
from companion.protocol import (PROFILE_UUID, RESPONSE_UUID, STATUS_UUID, Reassembler,
                                decode_json, encode_json, fragment)
from companion.settings import Settings
from bleak.exc import BleakError


def manager():
    return BluetoothManager(Settings(auto_connect=False), Mock(), Mock(), Mock(), Mock())


@pytest.mark.parametrize("platform, expected", [("win32", "AA:BB:CC:DD:EE:FF"), ("linux", None)])
def test_saved_address_can_recover_a_windows_link_without_advertising(monkeypatch, platform, expected):
    async def scenario():
        scan = AsyncMock(return_value={})
        monkeypatch.setattr(bluetooth, "sys", SimpleNamespace(platform=platform))
        monkeypatch.setattr(bluetooth, "BleakScanner", SimpleNamespace(discover=scan))
        central = manager()
        central.settings.device_address = "AA:BB:CC:DD:EE:FF"
        assert await central._find_device() == expected
        scan.assert_awaited_once_with(timeout=7.0, return_adv=True, service_uuids=[SERVICE_UUID])
    asyncio.run(scenario())


def test_discovery_still_selects_only_the_configured_nexus_service(monkeypatch):
    async def scenario():
        deck = SimpleNamespace(address="AA:BB:CC:DD:EE:FF")
        foreign = SimpleNamespace(address="AA:BB:CC:DD:EE:00")
        scan = AsyncMock(return_value={
            1: (deck, SimpleNamespace(service_uuids=[SERVICE_UUID.upper()])),
            2: (foreign, SimpleNamespace(service_uuids=[]))})
        monkeypatch.setattr(bluetooth, "BleakScanner", SimpleNamespace(discover=scan))
        central = manager()
        central.settings.device_address = deck.address.lower()
        assert await central._find_device() is deck
    asyncio.run(scenario())


@pytest.mark.parametrize("code", ["05", "0F"])
def test_initial_status_waits_for_authenticated_encryption_without_downgrading(code):
    async def scenario():
        central = manager()
        status = {"name": "NEXUS DECK", "protocol": 1, "firmware": "1.3.0"}
        read = AsyncMock(side_effect=[BleakError(f"Protocol Error 0x{code}: security not ready"), encode_json(status)])
        central.client = SimpleNamespace(is_connected=True, read_gatt_char=read)
        assert await central._initial_status() == status
        assert read.call_count == 2
        assert all(call.args == (STATUS_UUID,) for call in read.call_args_list)
    asyncio.run(scenario())


@pytest.mark.parametrize("error", [BleakError("Protocol Error 0x03: forbidden"), OSError("Access denied")])
def test_initial_status_does_not_retry_other_security_or_io_errors(error):
    async def scenario():
        central = manager()
        read = AsyncMock(side_effect=error)
        central.client = SimpleNamespace(is_connected=True, read_gatt_char=read)
        with pytest.raises(type(error)) as raised:
            await central._initial_status()
        assert raised.value is error and read.call_count == 1
    asyncio.run(scenario())


def test_initial_status_security_wait_has_a_deadline(monkeypatch):
    async def scenario():
        central = manager()
        ticks = iter((0.0, 0.0, 8.1))
        monkeypatch.setattr(bluetooth, "time", SimpleNamespace(monotonic=lambda: next(ticks)))
        error = BleakError("Protocol Error 0x05: not authenticated")
        read = AsyncMock(side_effect=error)
        central.client = SimpleNamespace(is_connected=True, read_gatt_char=read)
        with pytest.raises(BleakError) as raised:
            await central._initial_status()
        assert raised.value is error and read.call_count == 1
    asyncio.run(scenario())


@pytest.mark.parametrize("saved_address", ["", "AA:BB:CC:DD:EE:FF"])
def test_connection_refreshes_attributes_and_reports_timeouts(monkeypatch, saved_address):
    async def scenario():
        central = manager()
        central.enabled = True
        central.settings.device_address = saved_address
        central._find_device = AsyncMock(return_value=SimpleNamespace(address="AA:BB:CC:DD:EE:FF"))
        peer = SimpleNamespace(connect=AsyncMock(side_effect=asyncio.TimeoutError()),
                               disconnect=AsyncMock())
        create = Mock(return_value=peer)
        monkeypatch.setattr(bluetooth, "BleakClient", create)
        states = []
        def record(kind, detail):
            states.append((kind, detail))
            if kind == "disconnected":
                central.running = False
        central.on_state = record
        await central.run()
        assert states[-1][0] == "disconnected"
        assert "timed out" in states[-1][1] and "powered on" in states[-1][1]
        assert central.client is None and not central.ready
        assert create.call_args.kwargs["winrt"] == {"use_cached_services": False}
        peer.disconnect.assert_awaited_once()
    asyncio.run(scenario())


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
