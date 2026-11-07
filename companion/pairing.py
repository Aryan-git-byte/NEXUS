"""Authenticated WinRT pairing and recovery from Windows' older GATT cache.

Bleak's stock WinRT backend supports only CONFIRM_ONLY. This narrow backend
override retains its connection/GATT code and implements PROVIDE_PIN instead.
It never downgrades the requested pairing protection level. A strictly checked
ATT declaration lets an already-paired 1.3.0 deck receive the cache-repair image
without deleting its bond or requiring another USB bootstrap.
"""
from __future__ import annotations

import asyncio
import logging
import re
import struct
import uuid
from collections.abc import Awaitable, Callable
from typing import Any

from bleak.backends.characteristic import BleakGATTCharacteristic
from bleak.backends.descriptor import BleakGATTDescriptor
from bleak.backends.winrt.client import BleakClientWinRT, FutureLike, _ensure_success
from bleak.exc import BleakError
from winrt.windows.devices.bluetooth import BluetoothCacheMode
from winrt.windows.devices.bluetooth.genericattributeprofile import (
    GattCommunicationStatus, GattSessionStatus,
)
from winrt.windows.devices.enumeration import (
    DeviceInformation, DevicePairingKinds, DevicePairingProtectionLevel,
    DevicePairingResultStatus,
)

from .protocol import (COMMAND_UUID, TELEMETRY_UUID, RESPONSE_UUID, PROFILE_UUID,
                       STATUS_UUID, SERVICE_UUID, FIRMWARE_UUID)

log = logging.getLogger(__name__)
PinProvider = Callable[[str], Awaitable[str | None]]


def valid_pin(value: Any) -> bool:
    return isinstance(value, str) and re.fullmatch(r"[0-9]{6}", value) is not None


class NexusBleakClientWinRT(BleakClientWinRT):
    def __init__(self, *args, **kwargs):
        self.pin_provider: PinProvider | None = kwargs.pop("nexus_pin_provider", None)
        self._paired_address = kwargs.pop("nexus_paired_address", False)
        self._cached_firmware_descriptor = None
        super().__init__(*args, **kwargs)
        # WinRT can retain a physical link after an app closes. Such a deck
        # stops advertising, but its saved paired address remains connectable.
        if self._paired_address:
            if not re.fullmatch(r"(?:[0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}", self.address):
                raise BleakError("Invalid saved Bluetooth address")
            self._device_info = int(self.address.replace(":", ""), 16)
        self._retry_on_services_changed = True

    async def _create_requester(self, bluetooth_address):
        requester = await super()._create_requester(bluetooth_address)
        if self._paired_address and not requester.device_information.pairing.is_paired:
            requester.close()
            raise BleakError("Saved deck is not paired; discover it in Connection settings")
        return requester

    async def _get_services(self, **kwargs):
        self._cached_firmware_descriptor = None
        try:
            collection = await super()._get_services(**kwargs)
        except OSError as exc:
            # ERROR_BAD_COMMAND is the observed Windows failure after adding
            # an attribute to a bonded service. Access/authentication errors
            # and unreachable-device errors must retain their normal behavior.
            if (getattr(exc, "winerror", 0) or 0) & 0xFFFFFFFF != 0x80070016:
                raise
            log.warning("Windows rejected GATT discovery; validating the saved NEXUS attributes")
            collection = await super()._get_services(
                service_cache_mode=BluetoothCacheMode.CACHED,
                cache_mode=BluetoothCacheMode.CACHED)
        try:
            service = collection.get_service(SERVICE_UUID)
            if service is None or any(service.get_characteristic(key) is None for key in
                    (COMMAND_UUID, TELEMETRY_UUID, RESPONSE_UUID, PROFILE_UUID, STATUS_UUID)):
                raise BleakError("Cached Bluetooth layout is incompatible with NEXUS DECK")
            await self._start_live_link(service.get_characteristic(STATUS_UUID))
            return await self._recover_firmware_control(collection, service)
        except BaseException:
            self._cached_firmware_descriptor = None
            # Bleak has not taken ownership of this collection yet. Dispose
            # it on failure/cancellation, including Service Changed retries.
            await asyncio.sleep(0.1)
            for service in collection:
                try:
                    service.obj.close()
                except OSError:
                    log.debug("Could not close a discarded GATT service", exc_info=True)
            raise

    async def _start_live_link(self, status):
        if self._session is None or self._session.session_status == GattSessionStatus.ACTIVE:
            return
        # Cached enumeration is entirely local and can leave a cold GATT
        # session CLOSED forever. A real ATT read initiates Windows' link.
        # This is deliberately UNCACHED; a cached value cannot prove contact.
        log.debug("Starting a live Bluetooth link with a protected Status read")
        result = await FutureLike(status.obj.read_value_with_cache_mode_async(
            BluetoothCacheMode.UNCACHED))
        if (result.status == GattCommunicationStatus.PROTOCOL_ERROR and
                result.protocol_error in (0x05, 0x0F)):
            # The request started the link while encryption is being restored.
            # BluetoothManager still requires a successful protected read
            # before enabling commands, telemetry or profile synchronization.
            return
        _ensure_success(result, "value", "Could not start the deck's live Bluetooth connection")

    async def _recover_firmware_control(self, collection, service):
        if service.get_characteristic(FIRMWARE_UUID) is not None:
            return collection
        status = service.get_characteristic(STATUS_UUID)
        values = [d for d in status.descriptors if d.uuid == FIRMWARE_UUID]
        declarations = [d for d in status.descriptors
                        if d.uuid == "00002803-0000-1000-8000-00805f9b34fb"]
        if not values and not declarations:
            # A cold pre-update cache may know only Status's original CCCD.
            # Read that one descriptor range from the device to locate an
            # appended OTA control, without forcing Windows' failing service
            # discovery. Existing 1.2 firmware simply returns its sole CCCD.
            result = await FutureLike(status.obj.get_descriptors_with_cache_mode_async(
                BluetoothCacheMode.UNCACHED))
            native = _ensure_success(result, "descriptors", "Could not refresh the Status descriptor range")
            for descriptor in native:
                if descriptor.attribute_handle not in collection.descriptors:
                    collection.add_descriptor(BleakGATTDescriptor(descriptor,
                        descriptor.attribute_handle, str(descriptor.uuid), status))
            values = [d for d in status.descriptors if d.uuid == FIRMWARE_UUID]
            declarations = [d for d in status.descriptors
                            if d.uuid == "00002803-0000-1000-8000-00805f9b34fb"]
        if len(values) != 1 or len(declarations) != 1:
            return collection
        value, declaration = values[0], declarations[0]
        result = await FutureLike(declaration.obj.read_value_with_cache_mode_async(
            BluetoothCacheMode.UNCACHED))
        raw = bytes(_ensure_success(result, "value", "Could not validate the OTA attribute declaration"))
        expected = struct.pack("<BH", 0x08, value.handle) + uuid.UUID(FIRMWARE_UUID).bytes[::-1]
        if declaration.handle + 1 != value.handle or raw != expected:
            raise BleakError("The cached firmware attribute has an invalid ATT declaration")
        # This is the device's actual value handle, misclassified by Windows.
        # Writes use WinRT's descriptor Write Request API for that same handle,
        # preserving ATT acknowledgements and the firmware's security checks.
        characteristic = BleakGATTCharacteristic(value.obj, value.handle, FIRMWARE_UUID,
            ["write"], lambda: self.mtu_size - 3, service)
        collection.add_characteristic(characteristic)
        self._cached_firmware_descriptor = value
        log.info("Recovered the OTA control from its verified on-device ATT declaration")
        return collection

    async def write_gatt_char(self, characteristic, data, response):
        descriptor = self._cached_firmware_descriptor
        if descriptor is not None and characteristic.uuid == FIRMWARE_UUID:
            if characteristic.handle != descriptor.handle or not response:
                raise BleakError("Recovered OTA writes require the validated handle and an ATT response")
            await super().write_gatt_descriptor(descriptor, data)
            return
        await super().write_gatt_char(characteristic, data, response)

    async def pair(self, **kwargs) -> None:
        if self._requester is None:
            raise BleakError("The Bluetooth pairing requester is unavailable")
        information = await DeviceInformation.create_from_id_async(
            self._requester.device_information.id)
        if information.pairing.is_paired:
            return
        if not information.pairing.can_pair or self.pin_provider is None:
            raise BleakError("Open NEXUS DECK's editor to enter the device passkey")

        custom = information.pairing.custom
        loop = asyncio.get_running_loop()
        requests: set[asyncio.Task] = set()

        async def enter_pin(args, deferral) -> None:
            try:
                if args.pairing_kind != DevicePairingKinds.PROVIDE_PIN:
                    return
                pin = await asyncio.wait_for(self.pin_provider(self.address), 65.0)
                if valid_pin(pin):
                    args.accept_with_pin(pin)
            except asyncio.CancelledError:
                raise
            except Exception as exc:
                log.info("Pairing prompt: %s", exc)
            finally:
                deferral.complete()

        def requested(sender, args) -> None:
            # WinRT can invoke this on a foreign thread. The deferral keeps args
            # valid while Qt receives the prompt and the user enters the PIN.
            deferral = args.get_deferral()

            def schedule() -> None:
                task = loop.create_task(enter_pin(args, deferral))
                requests.add(task)
                task.add_done_callback(requests.discard)

            try:
                loop.call_soon_threadsafe(schedule)
            except RuntimeError:
                deferral.complete()

        token = custom.add_pairing_requested(requested)
        try:
            result = await asyncio.wait_for(custom.pair_with_protection_level_async(
                DevicePairingKinds.PROVIDE_PIN,
                DevicePairingProtectionLevel.ENCRYPTION_AND_AUTHENTICATION), 70.0)
            if result.status not in (DevicePairingResultStatus.PAIRED,
                                     DevicePairingResultStatus.ALREADY_PAIRED):
                raise BleakError(f"Passkey pairing failed: {result.status.name}. "
                                 "Open Pair new PC on the deck and reconnect.")
        finally:
            custom.remove_pairing_requested(token)
            pending = list(requests)
            for task in pending:
                task.cancel()
            if pending:
                await asyncio.gather(*pending, return_exceptions=True)
