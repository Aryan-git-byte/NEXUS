"""Upload a validated application to an already-paired deck through production BLE.

Prefer the companion's Firmware window for normal use. Quit its tray instance
before running this CLI so that only one central owns the deck connection.
"""
from __future__ import annotations

import argparse
import asyncio
import ctypes
import json
import sys
import time
import uuid
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime, timezone
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from bleak.exc import BleakError
from companion.actions import ActionEngine
from companion.bluetooth import BluetoothManager
from companion.firmware import FirmwareError, FirmwareImage
from companion.monitor import MonitorEngine
from companion.profiles import ProfileManager
from companion.protocol import FIRMWARE_UUID, STATUS_UUID, decode_json
from companion.settings import Settings, atomic_json, data_directory


async def install(image: FirmwareImage, directory: Path, verify_failures: bool) -> dict:
    settings = Settings.load(directory)
    profiles = ProfileManager(directory)
    if profiles.errors:
        raise FirmwareError("Fix rejected profile files in the editor before updating")
    ready = asyncio.Event()
    def state(name: str, detail: str) -> None:
        print(f"{name}: {detail}", flush=True)
        if name == "connected":
            ready.set()
    engine = ActionEngine(settings)
    manager = BluetoothManager(settings, engine, state,
        lambda button, result: print(f"{button}: {result}", flush=True), lambda _address: None)
    manager.enabled = True
    manager.configure_profiles(profiles.snapshot(), settings.active_profile)
    loop = asyncio.get_running_loop()
    worker = ThreadPoolExecutor(max_workers=1, thread_name_prefix="NexusOtaTelemetry")
    monitor = await loop.run_in_executor(worker, MonitorEngine, settings, engine.audio)
    async def sample():
        while True:
            manager.latest_telemetry = await loop.run_in_executor(worker, monitor.sample)
            await asyncio.sleep(settings.telemetry_interval)
    async def status():
        if not manager.client or not manager.client.is_connected:
            raise FirmwareError("The deck disconnected during verification")
        return decode_json(bytes(await manager.client.read_gatt_char(STATUS_UUID)))
    tasks = [asyncio.create_task(sample()), asyncio.create_task(manager.run())]
    try:
        await asyncio.wait_for(ready.wait(), 120.0)
        before = await status()
        checks = {}
        if verify_failures:
            if before.get("ota") is not True or before.get("ota_confirmed") is not True:
                raise FirmwareError("A confirmed OTA-enabled boot is required for update checks")
            try:
                await manager._request(FIRMWARE_UUID, {"type": "ota_begin",
                    "sequence": manager._next_sequence(), "session": manager.session,
                    "transfer": uuid.uuid4().hex, "size": 1,
                    "sha256": image.sha256, "version": image.version}, "ota_ack")
            except BleakError as exc:
                if "Invalid firmware manifest" not in str(exc):
                    raise
                checks["invalid_manifest_rejected"] = True
            else:
                raise FirmwareError("The device accepted an invalid firmware manifest")
            cancelled = False
            def cancel_progress(value: int, _text: str) -> None:
                nonlocal cancelled
                if value >= 1 and not cancelled:
                    cancelled = True
                    manager.cancel_firmware_update()
            task = asyncio.create_task(manager.upload_firmware(image, cancel_progress))
            try:
                await task
            except asyncio.CancelledError:
                pass
            else:
                raise FirmwareError("The cancellation check unexpectedly installed firmware")
            retained = await status()
            if not cancelled or any(retained.get(key) != before.get(key)
                for key in ("firmware", "slot", "elf_sha256", "ota_confirmed")):
                raise FirmwareError("Cancellation did not retain the original confirmed image")
            checks["cancelled_transfer_retained_current_firmware"] = True
            print("Device checks passed: invalid manifest rejected; cancelled upload retained current firmware.", flush=True)
        last_progress = -10
        def progress(value: int, text: str) -> None:
            nonlocal last_progress
            if value >= 96 or value - last_progress >= 5:
                last_progress = value
                print(f"{value:3d}% {text}", flush=True)
        started = time.monotonic()
        result = await manager.upload_firmware(image, progress)
        result.update(success=True, observed_at=datetime.now(timezone.utc).isoformat(),
                      duration_seconds=round(time.monotonic() - started, 2),
                      initial_slot=before.get("slot"), final_device=await status(),
                      profiles_synchronized=len(profiles.profiles), checks=checks)
        return result
    finally:
        await manager.stop()
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)
        await loop.run_in_executor(worker, monitor.close)
        worker.shutdown(wait=True)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--image", default="firmware/build/nexus_deck.bin")
    parser.add_argument("--data-dir", help="Saved companion settings/profiles directory")
    parser.add_argument("--output", default="test-results/ota-upload.json")
    parser.add_argument("--check-only", action="store_true", help="Validate the file without connecting or writing")
    parser.add_argument("--verify-failures", action="store_true", help="Test invalid manifest and cancel before the full upload")
    args = parser.parse_args()
    initialized = False
    try:
        image = FirmwareImage.load(args.image)
        print(f"Validated firmware {image.version}: {len(image.data):,} bytes, SHA-256 {image.sha256}", flush=True)
        if args.check_only:
            return 0
        if sys.platform == "win32":
            ctypes.OleDLL("ole32").CoInitializeEx(None, 0)
            initialized = True
        result = asyncio.run(install(image, data_directory(args.data_dir), args.verify_failures))
        atomic_json(Path(args.output).resolve(), result)
        print(json.dumps(result, indent=2), flush=True)
        return 0
    except (Exception, KeyboardInterrupt) as exc:
        print(f"Firmware upload failed: {exc}", file=sys.stderr)
        return 1
    finally:
        if initialized:
            ctypes.OleDLL("ole32").CoUninitialize()


if __name__ == "__main__":
    raise SystemExit(main())
