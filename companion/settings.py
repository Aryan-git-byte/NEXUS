"""Local configuration and atomic writes; BLE has no access to these settings."""
from __future__ import annotations
import json
import os
import sys
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any


def data_directory(override: str | None = None) -> Path:
    if override:
        return Path(override).expanduser().resolve()
    base = Path(os.environ.get("LOCALAPPDATA", str(Path.home() / ".local" / "share")))
    return base / "NexusDeck"


def resource_directory() -> Path:
    return Path(getattr(sys, "_MEIPASS", Path(__file__).resolve().parents[1]))


def atomic_json(path: Path, value: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_name(path.name + ".tmp")
    try:
        with temp.open("w", encoding="utf-8", newline="\n") as handle:
            json.dump(value, handle, ensure_ascii=False, indent=2, allow_nan=False)
            handle.write("\n")
            handle.flush()
            os.fsync(handle.fileno())
        os.replace(temp, path)
    finally:
        temp.unlink(missing_ok=True)


@dataclass
class Settings:
    device_address: str = ""
    auto_connect: bool = True
    active_profile: str = "default"
    telemetry_interval: float = 1.5
    project_directory: str = ""
    dev_server_executable: str = ""
    dev_server_arguments: list[str] | None = None
    disk_path: str = "C:\\"

    @classmethod
    def load(cls, directory: Path) -> "Settings":
        path = directory / "settings.json"
        if not path.exists():
            return cls()
        if path.stat().st_size > 16384:
            raise ValueError("Settings file is too large")
        value = json.loads(path.read_text("utf-8"))
        if not isinstance(value, dict) or set(value) - set(cls.__dataclass_fields__):
            raise ValueError("Unrecognized settings fields")
        result = cls(**value)
        for key in ("device_address", "active_profile", "project_directory",
                    "dev_server_executable", "disk_path"):
            field = getattr(result, key)
            if not isinstance(field, str) or len(field) > 1024 or "\x00" in field:
                raise ValueError(f"Invalid {key}")
        if type(result.auto_connect) is not bool:
            raise ValueError("Invalid auto_connect")
        if type(result.telemetry_interval) not in (float, int) or not 1 <= result.telemetry_interval <= 2:
            raise ValueError("Telemetry interval must be 1–2 seconds")
        if result.dev_server_arguments is not None and (
            not isinstance(result.dev_server_arguments, list)
            or len(result.dev_server_arguments) > 32
            or any(not isinstance(x, str) or len(x) > 1024 or "\x00" in x
                   for x in result.dev_server_arguments)):
            raise ValueError("Invalid development server arguments")
        return result

    def save(self, directory: Path) -> None:
        atomic_json(directory / "settings.json", asdict(self))
