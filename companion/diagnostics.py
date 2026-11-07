"""Validate and present the device's actual boot diagnostics."""
from __future__ import annotations

import re
from dataclasses import dataclass
from typing import Any

RESET_REASONS = {
    "unknown": "Unknown", "power_on": "Power on", "external": "Reset pin",
    "software": "Software restart", "panic": "Firmware panic",
    "interrupt_watchdog": "Interrupt watchdog", "task_watchdog": "Task watchdog",
    "watchdog": "Watchdog", "deep_sleep": "Deep sleep wake",
    "brownout": "Brownout (low voltage)", "sdio": "SDIO reset",
    "usb": "USB reset", "jtag": "JTAG reset", "efuse": "eFuse error",
    "power_glitch": "Power glitch", "cpu_lockup": "CPU lockup",
}


@dataclass(frozen=True, slots=True)
class BootDiagnostics:
    boot_id: str
    reason: str
    uptime_ms: int

    @classmethod
    def parse(cls, value: dict[str, Any]) -> "BootDiagnostics | None":
        boot_id, reason, uptime = (value.get(key) for key in ("boot_id", "reset_reason", "uptime_ms"))
        if (not isinstance(boot_id, str) or re.fullmatch(r"[0-9a-f]{8}", boot_id) is None or
                not isinstance(reason, str) or reason not in RESET_REASONS or
                type(uptime) is not int or not 0 <= uptime <= 0xFFFFFFFFFFFFFFFF):
            return None
        return cls(boot_id, reason, uptime)

    def summary(self) -> str:
        seconds = self.uptime_ms // 1000
        hours, seconds = divmod(seconds, 3600)
        minutes, seconds = divmod(seconds, 60)
        age = f"{hours}h {minutes}m" if hours else f"{minutes}m {seconds}s"
        return f"Last reset: {RESET_REASONS[self.reason]} · Reported uptime {age}"
