"""Exercise the actual WinRT backend override without starting a BLE radio."""
import asyncio
import sys
from types import SimpleNamespace
from unittest.mock import Mock

import pytest

pytestmark = pytest.mark.skipif(sys.platform != "win32", reason="WinRT pairing is Windows-only")
if sys.platform == "win32":
    from companion import pairing
    from bleak import BleakClient
    from bleak.exc import BleakError


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
