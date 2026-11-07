"""Exercise the production updater through a fragmented, retrying ATT peer."""
import asyncio
import base64
from types import SimpleNamespace
from unittest.mock import Mock

import pytest
from bleak.exc import BleakError

from companion.bluetooth import BluetoothManager, RequestRejected
from companion.firmware import FirmwareError, FirmwareImage
from companion.protocol import (FIRMWARE_UUID, STATUS_UUID, Reassembler,
                                decode_json, encode_json, fragment)
from companion.settings import Settings


class OtaPeer:
    def __init__(self, central, image, mtu=247):
        self.central, self.image = central, image
        self.mtu_size = mtu
        self.is_connected = True
        self.services = SimpleNamespace(get_characteristic=lambda _: object())
        self.assembler = Reassembler(8192)
        self.data = bytearray()
        self.messages = []
        self.chunks = []
        self.offset_error = False
        self.lose_chunk_ack = False
        self.lost_ack = False
        self.block_chunk = False
        self.chunk_waiting = asyncio.Event()
        self.boot_slot = "ota_0"
        self.boot_hash = image.elf_sha256
        self.abort_count = 0
        self.packet_count = 0
        self.reboot_task = None

    async def write_gatt_char(self, channel, packet, response):
        assert channel == FIRMWARE_UUID and response
        assert len(packet) <= self.mtu_size - 3
        self.packet_count += 1
        payload = self.assembler.feed(packet)
        if payload is None:
            return
        message = decode_json(payload)
        self.messages.append(message)
        reply = {"type": "ota_ack", "sequence": message["sequence"],
                 "session": message["session"], "status": "success",
                 "message": "Accepted", "slot": "ota_0", "offset": len(self.data),
                 "max_chunk": 4096, "restarting": False}
        if message["type"] == "ota_begin":
            assert message["sha256"] == self.image.sha256 and message["size"] == len(self.image.data)
        elif message["type"] == "ota_chunk":
            block = base64.b64decode(message["data"], validate=True)
            self.chunks.append((message["sequence"], message["offset"], block))
            if message["offset"] == len(self.data):
                self.data += block
            else:
                assert self.data[message["offset"]:message["offset"] + len(block)] == block
            reply["offset"] = len(self.data) + int(self.offset_error)
            if self.block_chunk:
                self.chunk_waiting.set()
                await asyncio.Future()
            if self.lose_chunk_ack and not self.lost_ack:
                self.lost_ack = True
                return
        elif message["type"] == "ota_end":
            assert self.data == self.image.data
            reply["restarting"] = True
            self.reboot_task = asyncio.create_task(self.reboot())
        elif message["type"] == "ota_abort":
            self.abort_count += 1
        for part in fragment(encode_json(reply), self.packet_count % 65535 + 1, self.mtu_size - 3):
            self.central._notify(SimpleNamespace(uuid=STATUS_UUID), bytearray(part))

    async def reboot(self):
        self.is_connected = False
        self.central.ready = False
        await asyncio.sleep(0.01)
        async with self.central.sync_lock:
            self.central.session = "b" * 32
            self.is_connected = True
            self.central.ready = True

    async def disconnect(self):
        self.is_connected = False
        self.central.ready = False

    async def read_gatt_char(self, channel):
        assert channel == STATUS_UUID
        return encode_json({"firmware": self.image.version, "slot": self.boot_slot,
                            "elf_sha256": self.boot_hash, "ota": True, "ota_confirmed": True})


def connected(image, mtu=247):
    central = BluetoothManager(Settings(auto_connect=False), Mock(), Mock(), Mock(), Mock())
    central.session = "a" * 32
    central.ready = True
    central.device_info = {"ota": True, "ota_confirmed": True}
    peer = OtaPeer(central, image, mtu)
    central.client = peer
    request = central._request
    async def short_request(channel, message, expected, timeout=12.0):
        return await request(channel, message, expected, timeout=0.02)
    central._request = short_request
    return central, peer


@pytest.mark.parametrize("mtu", [23, 247])
def test_fragmented_update_retries_and_verifies_real_boot_identity(firmware_blob, mtu):
    async def scenario():
        image = FirmwareImage.parse(firmware_blob)
        central, peer = connected(image, mtu)
        peer.lose_chunk_ack = True
        updates = []
        result = await asyncio.wait_for(central.upload_firmware(image,
            lambda value, text: updates.append((value, text))), 5.0)
        assert result == {"version": "1.3.0", "slot": "ota_0", "bytes": len(firmware_blob),
                          "sha256": image.sha256, "boot_confirmed": True}
        assert peer.data == firmware_blob and peer.abort_count == 0
        assert peer.chunks[0] == peer.chunks[1]
        assert updates[-1][0] == 100 and all(value < 100 for value, _ in updates[:-1])
        assert not central.pending and not central.updating and central.update_task is None
        assert peer.packet_count > len(peer.messages)
    asyncio.run(scenario())


def test_wrong_offset_aborts_inactive_image(firmware_blob):
    async def scenario():
        central, peer = connected(FirmwareImage.parse(firmware_blob))
        peer.offset_error = True
        with pytest.raises(FirmwareError, match="offset"):
            await asyncio.wait_for(central.upload_firmware(peer.image, Mock()), 5.0)
        assert peer.abort_count == 1 and peer.reboot_task is None
        assert not central.pending and not central.updating
    asyncio.run(scenario())


def test_user_cancellation_sends_abort_and_keeps_current_firmware(firmware_blob):
    async def scenario():
        central, peer = connected(FirmwareImage.parse(firmware_blob))
        peer.block_chunk = True
        task = asyncio.create_task(central.upload_firmware(peer.image, Mock()))
        await asyncio.wait_for(peer.chunk_waiting.wait(), 2.0)
        central.cancel_firmware_update()
        with pytest.raises(asyncio.CancelledError):
            await task
        assert peer.abort_count == 1 and peer.reboot_task is None
        assert not central.pending and not central.updating
    asyncio.run(scenario())


@pytest.mark.parametrize("wrong_identity", ["slot", "hash"])
def test_rollback_or_wrong_image_is_not_reported_as_success(firmware_blob, wrong_identity):
    async def scenario():
        central, peer = connected(FirmwareImage.parse(firmware_blob))
        if wrong_identity == "slot":
            peer.boot_slot = "factory"
        else:
            peer.boot_hash = "0" * 64
        with pytest.raises(FirmwareError, match="another firmware"):
            await asyncio.wait_for(central.upload_firmware(peer.image, Mock()), 5.0)
        assert peer.abort_count == 0 and central.update_committed
    asyncio.run(scenario())


@pytest.mark.parametrize("unavailable", ["offline", "old", "unconfirmed"])
def test_update_preconditions_prevent_all_radio_writes(firmware_blob, unavailable):
    async def scenario():
        central, peer = connected(FirmwareImage.parse(firmware_blob))
        if unavailable == "offline":
            central.ready = False
        elif unavailable == "old":
            central.device_info["ota"] = False
        else:
            central.device_info["ota_confirmed"] = False
        with pytest.raises(FirmwareError):
            await central.upload_firmware(peer.image, Mock())
        assert peer.messages == [] and peer.packet_count == 0
    asyncio.run(scenario())


@pytest.mark.parametrize("phase", ["ota_begin", "ota_chunk"])
def test_transfer_timeout_reports_acknowledged_bytes_and_retains_current_image(firmware_blob, phase):
    async def scenario():
        central, peer = connected(FirmwareImage.parse(firmware_blob))
        request = central._request
        async def interrupted(channel, message, expected, timeout=12.0):
            if message["type"] == phase:
                raise asyncio.TimeoutError()
            return await request(channel, message, expected, timeout)
        central._request = interrupted
        with pytest.raises(FirmwareError, match="0 / .*bytes: Bluetooth operation timed out") as raised:
            await central.upload_firmware(peer.image, Mock())
        assert "current firmware remains selected" in str(raised.value)
        assert peer.reboot_task is None and not central.update_committed
        assert not central.updating and not central.update_finishing
    asyncio.run(scenario())


def test_lost_completion_ack_still_requires_verified_new_boot(firmware_blob):
    async def scenario():
        central, peer = connected(FirmwareImage.parse(firmware_blob))
        request = central._request
        async def missing_completion(channel, message, expected, timeout=12.0):
            result = await request(channel, message, expected, timeout)
            if message["type"] == "ota_end":
                raise asyncio.TimeoutError()
            return result
        central._request = missing_completion
        result = await asyncio.wait_for(central.upload_firmware(peer.image, Mock()), 5.0)
        assert result["boot_confirmed"] is True and result["slot"] == "ota_0"
        assert peer.abort_count == 0 and central.update_committed
        assert not central.pending and not central.update_finishing
    asyncio.run(scenario())


def test_completion_rejection_aborts_instead_of_claiming_a_commit(firmware_blob):
    async def scenario():
        central, peer = connected(FirmwareImage.parse(firmware_blob))
        request = central._request
        async def rejected(channel, message, expected, timeout=12.0):
            if message["type"] == "ota_end":
                raise RequestRejected("Firmware checksum did not match")
            return await request(channel, message, expected, timeout)
        central._request = rejected
        with pytest.raises(FirmwareError, match="checksum did not match"):
            await central.upload_firmware(peer.image, Mock())
        assert peer.abort_count == 1 and peer.reboot_task is None
        assert not central.update_committed and not central.update_finishing
    asyncio.run(scenario())


def test_interrupted_commit_does_not_promise_original_image_was_retained(firmware_blob):
    async def scenario():
        central, peer = connected(FirmwareImage.parse(firmware_blob))
        request = central._request
        async def interrupted(channel, message, expected, timeout=12.0):
            if message["type"] == "ota_end":
                assert central.update_finishing
                central.cancel_firmware_update()
                assert not asyncio.current_task().cancelling()
                raise asyncio.CancelledError()
            return await request(channel, message, expected, timeout)
        central._request = interrupted
        with pytest.raises(FirmwareError, match="check which image is running") as raised:
            await central.upload_firmware(peer.image, Mock())
        assert "retained" not in str(raised.value) and peer.abort_count == 0
        assert not central.updating and not central.update_finishing
    asyncio.run(scenario())


def test_reboot_status_timeout_has_readable_feedback_without_retention_claim(firmware_blob):
    async def scenario():
        central, peer = connected(FirmwareImage.parse(firmware_blob))
        async def unavailable(_channel):
            raise asyncio.TimeoutError()
        peer.read_gatt_char = unavailable
        with pytest.raises(FirmwareError, match="reboot verification failed: Bluetooth operation timed out") as raised:
            await asyncio.wait_for(central.upload_firmware(peer.image, Mock()), 5.0)
        assert "remains selected" not in str(raised.value) and peer.abort_count == 0
        assert central.update_committed and not central.update_finishing
    asyncio.run(scenario())
