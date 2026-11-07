"""Exercise the actual WinRT backend override without starting a BLE radio."""
import asyncio
import struct
import sys
import uuid
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock

import pytest
from companion.protocol import (COMMAND_UUID, TELEMETRY_UUID, RESPONSE_UUID,
                                PROFILE_UUID, STATUS_UUID, SERVICE_UUID, FIRMWARE_UUID)

pytestmark = pytest.mark.skipif(sys.platform != "win32", reason="WinRT pairing is Windows-only")
if sys.platform == "win32":
    from companion import pairing
    from bleak import BleakClient
    from bleak.exc import BleakError
    from bleak.backends.service import BleakGATTService, BleakGATTServiceCollection
    from bleak.backends.characteristic import BleakGATTCharacteristic
    from bleak.backends.descriptor import BleakGATTDescriptor
    from winrt.windows.devices.bluetooth.genericattributeprofile import GattCommunicationStatus, GattSessionStatus


@pytest.mark.parametrize("pin,expected", [
    ("012345", True), ("999999", True), ("12345", False), ("1234567", False),
    ("１２３４５６", False), (123456, False), (None, False), ("12 456", False),
])
def test_pin_requires_exactly_six_ascii_digits(pin, expected):
    assert pairing.valid_pin(pin) is expected


@pytest.mark.parametrize("pin", ["012345", None])
def test_pairing_keeps_authentication_and_completes_event_deferral(monkeypatch, pin):
    async def scenario():
        completed = asyncio.Event()
        accepted = Mock()
        deferral = SimpleNamespace(complete=completed.set)
        args = SimpleNamespace(pairing_kind=pairing.DevicePairingKinds.PROVIDE_PIN,
                               get_deferral=lambda: deferral, accept_with_pin=accepted)

        class CustomPairing:
            def add_pairing_requested(self, callback):
                self.callback = callback
                return 45

            def remove_pairing_requested(self, token):
                assert token == 45
                self.removed = True

            async def pair_with_protection_level_async(self, ceremony, level):
                assert ceremony == pairing.DevicePairingKinds.PROVIDE_PIN
                assert level == pairing.DevicePairingProtectionLevel.ENCRYPTION_AND_AUTHENTICATION
                self.callback(self, args)
                await completed.wait()
                status = (pairing.DevicePairingResultStatus.PAIRED if accepted.called else
                          SimpleNamespace(name="REJECTED_BY_HANDLER"))
                return SimpleNamespace(status=status)

        custom = CustomPairing()
        information = SimpleNamespace(pairing=SimpleNamespace(is_paired=False, can_pair=True, custom=custom))

        async def create(device_id):
            assert device_id == "test-device"
            return information

        async def provide(address):
            assert address == "AA:BB:CC:DD:EE:FF"
            return pin

        monkeypatch.setattr(pairing, "DeviceInformation", SimpleNamespace(create_from_id_async=create))
        client = BleakClient("AA:BB:CC:DD:EE:FF", backend=pairing.NexusBleakClientWinRT,
                             nexus_pin_provider=provide)._backend
        client._requester = SimpleNamespace(device_information=SimpleNamespace(id="test-device"))
        if pin is None:
            with pytest.raises(BleakError, match="Passkey pairing failed"):
                await client.pair()
            accepted.assert_not_called()
        else:
            await client.pair()
            accepted.assert_called_once_with(pin)
        assert completed.is_set() and custom.removed
    asyncio.run(scenario())


def cached_layout(raw=None, descriptors=True):
    """The observed 1.2 -> 1.3 Windows cache, using real Bleak containers."""
    service = BleakGATTService(SimpleNamespace(close=Mock()), 14, SERVICE_UUID)
    collection = BleakGATTServiceCollection()
    collection.add_service(service)
    for handle, key in zip((16, 19, 21, 23, 25),
                           (COMMAND_UUID, TELEMETRY_UUID, RESPONSE_UUID, PROFILE_UUID, STATUS_UUID)):
        collection.add_characteristic(BleakGATTCharacteristic(object(), handle, key,
            ["write"], lambda: 244, service))
    status = service.get_characteristic(STATUS_UUID)
    if raw is None:
        raw = struct.pack("<BH", 8, 28) + uuid.UUID(FIRMWARE_UUID).bytes[::-1]
    declaration = SimpleNamespace(uuid=uuid.UUID("00002803-0000-1000-8000-00805f9b34fb"),
        attribute_handle=27, read_value_with_cache_mode_async=AsyncMock(return_value=
        SimpleNamespace(status=GattCommunicationStatus.SUCCESS, value=raw)))
    value = SimpleNamespace(uuid=uuid.UUID(FIRMWARE_UUID), attribute_handle=28)
    status.obj = SimpleNamespace(get_descriptors_with_cache_mode_async=AsyncMock(return_value=
        SimpleNamespace(status=GattCommunicationStatus.SUCCESS, descriptors=[declaration, value])),
        read_value_with_cache_mode_async=AsyncMock(return_value=SimpleNamespace(
            status=GattCommunicationStatus.SUCCESS, protocol_error=None, value=b"protected status")))
    if descriptors:
        collection.add_descriptor(BleakGATTDescriptor(declaration, 27,
            "00002803-0000-1000-8000-00805f9b34fb", status))
        collection.add_descriptor(BleakGATTDescriptor(value, 28, FIRMWARE_UUID, status))
    return collection


def command_error(code=-2147024874):
    error = OSError("Windows GATT enumeration failed")
    error.winerror = code
    return error


def test_windows_cache_recovery_uses_the_device_declaration_and_acknowledged_writes(monkeypatch):
    async def scenario():
        collection = cached_layout()
        enumerate_services = AsyncMock(side_effect=[command_error(), collection])
        write_descriptor, write_char = AsyncMock(), AsyncMock()
        monkeypatch.setattr(pairing.BleakClientWinRT, "_get_services", enumerate_services)
        monkeypatch.setattr(pairing.BleakClientWinRT, "write_gatt_descriptor", write_descriptor)
        monkeypatch.setattr(pairing.BleakClientWinRT, "write_gatt_char", write_char)
        monkeypatch.setattr(pairing, "FutureLike", lambda operation: operation)
        backend = BleakClient("AA:BB:CC:DD:EE:FF", backend=pairing.NexusBleakClientWinRT)._backend
        assert backend._retry_on_services_changed
        restored = await backend._get_services(service_cache_mode=pairing.BluetoothCacheMode.UNCACHED,
                                               cache_mode=pairing.BluetoothCacheMode.UNCACHED)
        firmware = restored.get_characteristic(FIRMWARE_UUID)
        assert firmware.handle == 28 and firmware.properties == ["write"]
        assert enumerate_services.call_count == 2
        assert enumerate_services.call_args.kwargs == {
            "service_cache_mode": pairing.BluetoothCacheMode.CACHED,
            "cache_mode": pairing.BluetoothCacheMode.CACHED}
        await backend.write_gatt_char(firmware, b"firmware fragment", True)
        write_descriptor.assert_awaited_once_with(collection.get_descriptor(28), b"firmware fragment")
        write_char.assert_not_called()
        with pytest.raises(BleakError, match="ATT response"):
            await backend.write_gatt_char(firmware, b"firmware fragment", False)
        await backend.write_gatt_char(collection.get_characteristic(PROFILE_UUID), b"profile", True)
        write_char.assert_awaited_once()
    asyncio.run(scenario())


@pytest.mark.parametrize("cold", [False, True])
def test_cached_connection_recovers_an_appended_control_without_forced_service_discovery(monkeypatch, cold):
    async def scenario():
        collection = cached_layout(descriptors=not cold)
        enumerate_services = AsyncMock(return_value=collection)
        monkeypatch.setattr(pairing.BleakClientWinRT, "_get_services", enumerate_services)
        monkeypatch.setattr(pairing, "FutureLike", lambda operation: operation)
        backend = BleakClient("AA:BB:CC:DD:EE:FF", backend=pairing.NexusBleakClientWinRT)._backend
        restored = await backend._get_services(cache_mode=pairing.BluetoothCacheMode.CACHED)
        assert restored.get_characteristic(FIRMWARE_UUID).handle == 28
        assert enumerate_services.call_count == 1
        native_descriptors = collection.get_characteristic(STATUS_UUID).obj.get_descriptors_with_cache_mode_async
        assert native_descriptors.call_count == int(cold)
        if cold:
            native_descriptors.assert_awaited_once_with(pairing.BluetoothCacheMode.UNCACHED)
    asyncio.run(scenario())


def test_current_six_control_cache_needs_no_migration_requests(monkeypatch):
    async def scenario():
        collection = cached_layout(descriptors=False)
        service = collection.get_service(SERVICE_UUID)
        collection.add_characteristic(BleakGATTCharacteristic(object(), 28, FIRMWARE_UUID,
            ["write"], lambda: 244, service))
        monkeypatch.setattr(pairing.BleakClientWinRT, "_get_services", AsyncMock(return_value=collection))
        backend = BleakClient("AA:BB:CC:DD:EE:FF", backend=pairing.NexusBleakClientWinRT)._backend
        assert await backend._get_services() is collection
        collection.get_characteristic(STATUS_UUID).obj.get_descriptors_with_cache_mode_async.assert_not_called()
        assert backend._cached_firmware_descriptor is None
    asyncio.run(scenario())


@pytest.mark.parametrize("protocol_error", [None, 0x05, 0x0F])
def test_cold_cached_session_requires_an_on_device_read_before_connect_can_finish(monkeypatch, protocol_error):
    async def scenario():
        collection = cached_layout(descriptors=False)
        service = collection.get_service(SERVICE_UUID)
        collection.add_characteristic(BleakGATTCharacteristic(object(), 28, FIRMWARE_UUID,
            ["write"], lambda: 244, service))
        read = collection.get_characteristic(STATUS_UUID).obj.read_value_with_cache_mode_async
        if protocol_error is not None:
            read.return_value = SimpleNamespace(status=GattCommunicationStatus.PROTOCOL_ERROR,
                                               protocol_error=protocol_error)
        monkeypatch.setattr(pairing.BleakClientWinRT, "_get_services", AsyncMock(return_value=collection))
        monkeypatch.setattr(pairing, "FutureLike", lambda operation: operation)
        backend = BleakClient("AA:BB:CC:DD:EE:FF", backend=pairing.NexusBleakClientWinRT)._backend
        backend._session = SimpleNamespace(session_status=GattSessionStatus.CLOSED)
        assert await backend._get_services(cache_mode=pairing.BluetoothCacheMode.CACHED) is collection
        read.assert_awaited_once_with(pairing.BluetoothCacheMode.UNCACHED)
        # Reading local metadata must never synthesize an ACTIVE link or mark
        # discovery complete. Bleak still waits for the real session event.
        assert not backend.is_connected and backend.services is None
        service.obj.close.assert_not_called()
    asyncio.run(scenario())


@pytest.mark.parametrize("status_name, protocol_error", [
    ("UNREACHABLE", None), ("ACCESS_DENIED", None), ("PROTOCOL_ERROR", 0x03),
])
def test_cold_link_read_does_not_ignore_unreachable_or_forbidden_devices(monkeypatch, status_name, protocol_error):
    async def scenario():
        collection = cached_layout(descriptors=False)
        service = collection.get_service(SERVICE_UUID)
        read = collection.get_characteristic(STATUS_UUID).obj.read_value_with_cache_mode_async
        read.return_value = SimpleNamespace(status=getattr(GattCommunicationStatus, status_name),
                                           protocol_error=protocol_error)
        monkeypatch.setattr(pairing.BleakClientWinRT, "_get_services", AsyncMock(return_value=collection))
        monkeypatch.setattr(pairing, "FutureLike", lambda operation: operation)
        backend = BleakClient("AA:BB:CC:DD:EE:FF", backend=pairing.NexusBleakClientWinRT)._backend
        backend._session = SimpleNamespace(session_status=GattSessionStatus.CLOSED)
        with pytest.raises(BleakError, match="Could not start the deck"):
            await backend._get_services(cache_mode=pairing.BluetoothCacheMode.CACHED)
        read.assert_awaited_once_with(pairing.BluetoothCacheMode.UNCACHED)
        service.obj.close.assert_called_once()
        assert backend._cached_firmware_descriptor is None
    asyncio.run(scenario())


def test_service_change_cancellation_disposes_unclaimed_native_services(monkeypatch):
    async def scenario():
        collection = cached_layout()
        service = collection.get_service(SERVICE_UUID)
        read = collection.get_characteristic(STATUS_UUID).obj.read_value_with_cache_mode_async
        read.side_effect = asyncio.CancelledError()
        monkeypatch.setattr(pairing.BleakClientWinRT, "_get_services", AsyncMock(return_value=collection))
        monkeypatch.setattr(pairing, "FutureLike", lambda operation: operation)
        backend = BleakClient("AA:BB:CC:DD:EE:FF", backend=pairing.NexusBleakClientWinRT)._backend
        backend._session = SimpleNamespace(session_status=GattSessionStatus.CLOSED)
        with pytest.raises(asyncio.CancelledError):
            await backend._get_services()
        service.obj.close.assert_called_once()
    asyncio.run(scenario())


def test_active_link_does_not_issue_an_extra_startup_read(monkeypatch):
    async def scenario():
        collection = cached_layout()
        service = collection.get_service(SERVICE_UUID)
        monkeypatch.setattr(pairing.BleakClientWinRT, "_get_services", AsyncMock(return_value=collection))
        monkeypatch.setattr(pairing, "FutureLike", lambda operation: operation)
        backend = BleakClient("AA:BB:CC:DD:EE:FF", backend=pairing.NexusBleakClientWinRT)._backend
        backend._session = SimpleNamespace(session_status=GattSessionStatus.ACTIVE, max_pdu_size=247)
        assert await backend._get_services() is collection
        collection.get_characteristic(STATUS_UUID).obj.read_value_with_cache_mode_async.assert_not_called()
        service.obj.close.assert_not_called()
    asyncio.run(scenario())


def test_legacy_firmware_does_not_acquire_an_invented_update_control(monkeypatch):
    async def scenario():
        collection = cached_layout(descriptors=False)
        status = collection.get_characteristic(STATUS_UUID)
        status.obj.get_descriptors_with_cache_mode_async.return_value.descriptors = []
        monkeypatch.setattr(pairing.BleakClientWinRT, "_get_services", AsyncMock(return_value=collection))
        monkeypatch.setattr(pairing, "FutureLike", lambda operation: operation)
        backend = BleakClient("AA:BB:CC:DD:EE:FF", backend=pairing.NexusBleakClientWinRT)._backend
        assert await backend._get_services() is collection
        assert collection.get_characteristic(FIRMWARE_UUID) is None
        assert backend._cached_firmware_descriptor is None
    asyncio.run(scenario())


@pytest.mark.parametrize("raw", [
    b"", struct.pack("<BH", 4, 28) + bytes(16),
    struct.pack("<BH", 8, 29) + uuid.UUID(FIRMWARE_UUID).bytes[::-1],
    struct.pack("<BH", 8, 28) + uuid.UUID(PROFILE_UUID).bytes[::-1],
])
def test_cache_recovery_rejects_invalid_device_declarations(monkeypatch, raw):
    async def scenario():
        collection = cached_layout(raw)
        monkeypatch.setattr(pairing.BleakClientWinRT, "_get_services",
                            AsyncMock(side_effect=[command_error(), collection]))
        monkeypatch.setattr(pairing, "FutureLike", lambda operation: operation)
        backend = BleakClient("AA:BB:CC:DD:EE:FF", backend=pairing.NexusBleakClientWinRT)._backend
        with pytest.raises(BleakError, match="invalid ATT declaration"):
            await backend._get_services()
        assert collection.get_characteristic(FIRMWARE_UUID) is None
    asyncio.run(scenario())


@pytest.mark.parametrize("code", [-2147024891, -2147024894])
def test_cache_recovery_does_not_swallow_other_windows_errors(monkeypatch, code):
    async def scenario():
        error = command_error(code)
        enumerate_services = AsyncMock(side_effect=error)
        monkeypatch.setattr(pairing.BleakClientWinRT, "_get_services", enumerate_services)
        backend = BleakClient("AA:BB:CC:DD:EE:FF", backend=pairing.NexusBleakClientWinRT)._backend
        with pytest.raises(OSError) as rejected:
            await backend._get_services()
        assert rejected.value is error and enumerate_services.call_count == 1
    asyncio.run(scenario())


@pytest.mark.parametrize("paired", [True, False])
def test_direct_saved_address_requires_an_existing_windows_bond(monkeypatch, paired):
    async def scenario():
        requester = SimpleNamespace(device_information=SimpleNamespace(
            pairing=SimpleNamespace(is_paired=paired)), close=Mock())
        create = AsyncMock(return_value=requester)
        monkeypatch.setattr(pairing.BleakClientWinRT, "_create_requester", create)
        backend = BleakClient("AA:BB:CC:DD:EE:FF", backend=pairing.NexusBleakClientWinRT,
                             nexus_paired_address=True)._backend
        assert backend._device_info == 0xAABBCCDDEEFF
        if paired:
            assert await backend._create_requester(backend._device_info) is requester
            requester.close.assert_not_called()
        else:
            with pytest.raises(BleakError, match="not paired"):
                await backend._create_requester(backend._device_info)
            requester.close.assert_called_once()
    asyncio.run(scenario())
