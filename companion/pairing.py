"""Authenticated WinRT passkey pairing, with the PIN collected by the Qt editor.

Bleak's stock WinRT backend supports only CONFIRM_ONLY. This narrow backend
override retains its connection/GATT code and implements PROVIDE_PIN instead.
It never downgrades the requested pairing protection level.
"""
from __future__ import annotations

import asyncio
import logging
import re
from collections.abc import Awaitable, Callable
from typing import Any

from bleak.backends.winrt.client import BleakClientWinRT
from bleak.exc import BleakError
from winrt.windows.devices.enumeration import (
    DeviceInformation, DevicePairingKinds, DevicePairingProtectionLevel,
    DevicePairingResultStatus,
)

log = logging.getLogger(__name__)
PinProvider = Callable[[str], Awaitable[str | None]]


def valid_pin(value: Any) -> bool:
    return isinstance(value, str) and re.fullmatch(r"[0-9]{6}", value) is not None


class NexusBleakClientWinRT(BleakClientWinRT):
    def __init__(self, *args, **kwargs):
        self.pin_provider: PinProvider | None = kwargs.pop("nexus_pin_provider", None)
        super().__init__(*args, **kwargs)

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
