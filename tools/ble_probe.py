"""Read real BLE advertisements and save evidence; never executes a macro."""
from __future__ import annotations
import argparse
import asyncio
import ctypes
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from companion.bluetooth import BluetoothManager
from bleak import BleakScanner


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--output", default="test-results/ble-advertising.json")
    parser.add_argument("--all", action="store_true", help="Include all nearby BLE service UUIDs for diagnostics")
    args = parser.parse_args()
    initialized = False
    result = {"observed_at": datetime.now(timezone.utc).isoformat(), "devices": [], "error": None}
    try:
        if not 1 <= args.timeout <= 30:
            raise ValueError("Use a scan duration between 1 and 30 seconds")
        if sys.platform == "win32":
            ctypes.OleDLL("ole32").CoInitializeEx(None, 0)
            initialized = True
        async def scan():
            if not args.all:
                return await BluetoothManager.discover(args.timeout)
            found = await BleakScanner.discover(timeout=args.timeout, return_adv=True)
            return [{"name": advert.local_name or device.name or "", "address": device.address,
                     "services": advert.service_uuids, "rssi": advert.rssi}
                    for device, advert in found.values()]
        result["devices"] = asyncio.run(scan())
    except Exception as exc:
        result["error"] = str(exc)
    finally:
        if initialized:
            ctypes.OleDLL("ole32").CoUninitialize()
    path = Path(args.output)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))
    return 0 if result["devices"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
