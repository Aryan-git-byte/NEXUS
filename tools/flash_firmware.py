"""Back up and flash NEXUS DECK, preserving the original touch/NVS partition.

Run with the Python interpreter from an ESP-IDF 5.5 terminal (esptool installed).
An offline --prepare-only mode validates migration against an existing backup.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import subprocess
import sys
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path

FLASH_SIZE = 16 * 1024 * 1024
TABLE_OFFSET = 0x8000
SECTOR = 4096


@dataclass(frozen=True)
class Partition:
    name: str
    type: int
    subtype: int
    offset: int
    size: int
    flags: int = 0

    @property
    def end(self) -> int:
        return self.offset + self.size


def parse_table(data: bytes) -> list[Partition]:
    if len(data) < 32:
        raise ValueError("Partition table is truncated")
    parts: list[Partition] = []
    for position in range(0, min(len(data), 0xC00), 32):
        entry = data[position:position + 32]
        if len(entry) != 32:
            raise ValueError("Partition entry is truncated")
        magic = struct.unpack_from("<H", entry)[0]
        if magic == 0xFFFF:
            break
        if magic == 0xEBEB:
            if hashlib.md5(data[:position]).digest() != entry[16:32]:
                raise ValueError("Partition table checksum is invalid")
            break
        if magic != 0x50AA:
            raise ValueError("Unrecognized partition table; no flash writes were made")
        _, kind, subtype, offset, size, raw_name, flags = struct.unpack("<HBBII16sI", entry)
        name = raw_name.split(b"\0", 1)[0].decode("ascii", "strict")
        if not name or offset < 0x9000 or size == 0 or offset % SECTOR or size % SECTOR or offset + size > FLASH_SIZE:
            raise ValueError("Invalid partition bounds")
        if any(p.name == name or max(p.offset, offset) < min(p.end, offset + size) for p in parts):
            raise ValueError("Duplicate or overlapping partitions")
        parts.append(Partition(name, kind, subtype, offset, size, flags))
    return parts


def migrated_partition(backup: bytes, old: Partition | None, new: Partition) -> bytes:
    if new.flags & 1 or (old and old.flags & 1):
        raise ValueError("Encrypted partition migration requires a separate encryption-aware procedure")
    if old is None:
        return b"\xff" * new.size
    if old.type != new.type or old.subtype != new.subtype or old.size > new.size or old.end > len(backup):
        raise ValueError("Cannot preserve this partition in the new layout")
    if new.name == "nvs" and old.offset != new.offset:
        # NVS page encryption/address changes must never be guessed.
        raise ValueError("NVS offsets differ; use an explicit export/import procedure")
    return backup[old.offset:old.end] + b"\xff" * (new.size - old.size)


def load_build(directory: Path) -> tuple[dict, list[Partition], dict[int, Path]]:
    directory = directory.resolve()
    args = json.loads((directory / "flasher_args.json").read_text(encoding="utf-8"))
    if args.get("extra_esptool_args", {}).get("chip") != "esp32s3":
        raise ValueError("Build does not target ESP32-S3")
    settings = args.get("flash_settings", {})
    if settings != {"flash_mode": "dio", "flash_size": "16MB", "flash_freq": "80m"}:
        raise ValueError("Unexpected flash configuration")
    files: dict[int, Path] = {}
    for address, filename in args["flash_files"].items():
        path = (directory / filename).resolve()
        if not path.is_relative_to(directory) or not path.is_file():
            raise ValueError("A required firmware image is missing or outside its build directory")
        offset = int(address, 0)
        if offset < 0 or offset % SECTOR or not 0 < path.stat().st_size <= FLASH_SIZE - offset:
            raise ValueError("Invalid flash image bounds")
        files[offset] = path
    if set(files) != {0, TABLE_OFFSET, 0x60000} or files[0].stat().st_size > TABLE_OFFSET:
        raise ValueError("Unexpected bootloader, partition table, or application offsets")
    if files[0x60000].read_bytes()[:1] != b"\xe9":
        raise ValueError("Application is not an ESP firmware image")
    parts = parse_table(files[TABLE_OFFSET].read_bytes())
    factory = next((p for p in parts if p.type == 0 and p.offset == 0x60000), None)
    if factory is None or files[0x60000].stat().st_size > factory.size:
        raise ValueError("Application does not fit its partition")
    return args, parts, files


def prepare(backup: bytes, parts: list[Partition], files: dict[int, Path], output: Path) -> dict:
    if len(backup) < TABLE_OFFSET + SECTOR:
        raise ValueError("Backup does not include the original partition table")
    old_parts = parse_table(backup[TABLE_OFFSET:TABLE_OFFSET + SECTOR])
    images = dict(files)
    preserved = []
    for name in ("nvs", "phy_init"):
        new = next((p for p in parts if p.name == name), None)
        old = next((p for p in old_parts if p.name == name), None)
        if new is None or (name == "nvs" and (new.type, new.subtype) != (1, 2)):
            raise ValueError("Expected NEXUS data partition is missing")
        image = migrated_partition(backup, old, new)
        path = output / f"{name}-preserved.bin"
        path.write_bytes(image)
        # New space must contain erased pages, not bytes of the original app.
        if old is None or (old.offset, old.size) != (new.offset, new.size):
            images[new.offset] = path
        preserved.append({"name": name, "old_bytes": old.size if old else 0,
                          "new_bytes": new.size, "sha256": hashlib.sha256(image).hexdigest()})
    spans = sorted((address, address + path.stat().st_size) for address, path in images.items())
    if any(end > start for (_, end), (start, _) in zip(spans, spans[1:])):
        raise ValueError("Flash images overlap")
    plan = {"old_partitions": [asdict(p) for p in old_parts],
            "new_partitions": [asdict(p) for p in parts], "preserved": preserved,
            "backup_sha256": hashlib.sha256(backup).hexdigest(),
            "images": [{"offset": f"0x{address:x}", "path": str(path.resolve()),
                        "size": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                       for address, path in sorted(images.items())]}
    (output / "flash-plan.json").write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    return plan


def run_esptool(port: str, arguments: list[str], output: Path, name: str, after: str = "no_reset") -> str:
    command = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "--port", port,
               "--baud", "460800", "--after", after, *arguments]
    result = subprocess.run(command, capture_output=True, text=True, errors="replace", timeout=300)
    text = result.stdout + result.stderr
    (output / f"{name}.log").write_text(text, encoding="utf-8")
    for line in text.splitlines():
        if "\b" not in line and not line.startswith("Writing at "):
            print(line)
    if result.returncode:
        raise RuntimeError(f"esptool {name} failed; see {output / (name + '.log')}")
    return text


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", help="ESP32-S3 ROM download port, for example COM17")
    parser.add_argument("--build-dir", default="firmware/build")
    parser.add_argument("--backup-dir", default="test-results/device-backup")
    parser.add_argument("--prepare-only", action="store_true", help="Validate migration without writing flash")
    parser.add_argument("--backup-file", help="Offline prepare-only input; never used directly for a device write")
    args = parser.parse_args()
    if args.backup_file and not args.prepare_only:
        parser.error("--backup-file is allowed only with --prepare-only")
    if not args.port and not args.backup_file:
        parser.error("Provide --port, or --prepare-only with --backup-file")
    try:
        _, parts, files = load_build(Path(args.build_dir))
        timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
        output = Path(args.backup_dir).resolve() / timestamp
        output.mkdir(parents=True, exist_ok=False)
        if args.backup_file:
            backup = Path(args.backup_file).read_bytes()
        else:
            table_path = output / "table-before.bin"
            run_esptool(args.port, ["read_flash", hex(TABLE_OFFSET), hex(SECTOR), str(table_path)], output, "read-table")
            old = parse_table(table_path.read_bytes())
            length = max(0x60000, max((p.end for p in old), default=0))
            backup_path = output / "before-flash.bin"
            run_esptool(args.port, ["read_flash", "0", hex(length), str(backup_path)], output, "backup")
            backup = backup_path.read_bytes()
            if len(backup) != length or backup[TABLE_OFFSET:TABLE_OFFSET + SECTOR] != table_path.read_bytes():
                raise ValueError("Backup verification failed; no flash writes were made")
        plan = prepare(backup, parts, files, output)
        print(f"Migration validated. Touch calibration/NVS preserved. Backup and plan: {output}")
        if args.prepare_only:
            return 0
        writes = ["write_flash", "--flash_mode", "dio", "--flash_size", "16MB", "--flash_freq", "80m"]
        for image in plan["images"]:
            writes += [image["offset"], image["path"]]
        log = run_esptool(args.port, writes, output, "flash", after="hard_reset")
        verified = len(re.findall("Hash of data verified", log)) == len(plan["images"])
        (output / "result.json").write_text(json.dumps({"flashed": True, "hash_verified": verified,
            "images": len(plan["images"]), "port": args.port, "display_touch_verified": False}, indent=2) + "\n")
        if not verified:
            raise RuntimeError("Flash finished but not every image reported a verified hash; inspect flash.log")
        print("Upload and hash verification succeeded. USB may disappear when the LCD takes GPIO19/20.")
        return 0
    except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"Flash stopped: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
