"""Only well-formed device diagnostics become UI text or log records."""
import pytest
from companion.diagnostics import BootDiagnostics, RESET_REASONS


@pytest.mark.parametrize("reason", list(RESET_REASONS))
def test_all_device_reset_causes_have_a_readable_label(reason):
    result = BootDiagnostics.parse({"boot_id": "12ab34cd", "reset_reason": reason, "uptime_ms": 3661000})
    assert result is not None and result.reason == reason
    assert RESET_REASONS[reason] in result.summary() and "1h 1m" in result.summary()


@pytest.mark.parametrize("field,value", [
    ("boot_id", None), ("boot_id", 12345678), ("boot_id", "1234567"),
    ("boot_id", "１２３４５６７８"), ("boot_id", "12AB34CD"),
    ("reset_reason", "untrusted\nlog text"), ("reset_reason", None),
    ("uptime_ms", True), ("uptime_ms", -1), ("uptime_ms", 1.5),
    ("uptime_ms", 0x10000000000000000),
])
def test_malformed_diagnostics_are_not_displayed_or_logged(field, value):
    data = {"boot_id": "12ab34cd", "reset_reason": "brownout", "uptime_ms": 61000}
    data[field] = value
    assert BootDiagnostics.parse(data) is None


def test_older_firmware_has_no_invented_boot_diagnostics():
    assert BootDiagnostics.parse({"name": "NEXUS DECK", "firmware": "1.3.0"}) is None


def test_zero_uptime_is_a_valid_fresh_boot():
    value = BootDiagnostics.parse({"boot_id": "00000000", "reset_reason": "software", "uptime_ms": 0})
    assert value is not None and "0m 0s" in value.summary()
