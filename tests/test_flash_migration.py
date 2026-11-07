"""Verify calibration-preserving partition expansion against corrupted inputs."""
import hashlib
import json
import struct
from types import SimpleNamespace

import pytest
from tools.flash_firmware import (OTA_DATA_OFFSET, OTA_LAYOUT, TABLE_OFFSET,
                                  Partition, backup_flash, detected_flash_size, load_build,
                                  migrated_partition, parse_table, prepare, run_esptool)


def entry(name, kind, subtype, offset, size):
    return struct.pack("<HBBII16sI", 0x50AA, kind, subtype, offset, size, name.encode(), 0)


def test_partition_table_and_checksum_validation():
    data = entry("nvs", 1, 2, 0x9000, 0x6000) + entry("factory", 0, 0, 0x10000, 0x100000)
    data += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(data).digest()
    parts = parse_table(data)
    assert parts[0] == Partition("nvs", 1, 2, 0x9000, 0x6000)
    corrupted = bytearray(data)
    corrupted[12] ^= 1
    with pytest.raises(ValueError, match="checksum"):
        parse_table(corrupted)


def test_expansion_keeps_all_nvs_pages_and_erases_old_app_bytes():
    old = Partition("nvs", 1, 2, 0x9000, 0x6000)
    new = Partition("nvs", 1, 2, 0x9000, 0x50000)
    backup = b"\xaa" * 0x9000 + bytes(range(256)) * 96 + b"OLD_APPLICATION" * 30000
    result = migrated_partition(backup, old, new)
    assert len(result) == 0x50000
    assert result[:0x6000] == backup[0x9000:0xF000]
    assert result[0x6000:] == b"\xff" * (0x50000 - 0x6000)
    with pytest.raises(ValueError, match="offsets differ"):
        migrated_partition(backup, old, Partition("nvs", 1, 2, 0xA000, 0x50000))
    with pytest.raises(ValueError, match="preserve"):
        migrated_partition(b"\xff" * 0x1000, old, new)


def ota_build(tmp_path, firmware_blob):
    files = {0: "bootloader.bin", TABLE_OFFSET: "partition-table.bin",
             0x60000: "nexus_deck.bin", OTA_DATA_OFFSET: "ota_data_initial.bin"}
    table = b"".join(entry(name, *bounds) for name, bounds in OTA_LAYOUT.items())
    table += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(table).digest()
    (tmp_path / "partition-table.bin").write_bytes(table + b"\xff" * (4096 - len(table)))
    (tmp_path / "bootloader.bin").write_bytes(b"\xe9" + bytes(100))
    (tmp_path / "nexus_deck.bin").write_bytes(firmware_blob)
    (tmp_path / "ota_data_initial.bin").write_bytes(b"\xff" * 8192)
    args = {"extra_esptool_args": {"chip": "esp32s3"},
            "flash_settings": {"flash_mode": "dio", "flash_size": "16MB", "flash_freq": "80m"},
            "flash_files": {hex(address): filename for address, filename in files.items()}}
    (tmp_path / "flasher_args.json").write_text(json.dumps(args), encoding="utf-8")
    return load_build(tmp_path)


def test_ota_bootstrap_preserves_all_user_partitions(tmp_path, firmware_blob):
    _, parts, files = ota_build(tmp_path, firmware_blob)
    old = [p for p in parts if p.name not in ("otadata", "ota_0", "ota_1")]
    backup = bytearray(b"\xff" * 0x660000)
    table = b"".join(entry(p.name, p.type, p.subtype, p.offset, p.size) for p in old)
    backup[TABLE_OFFSET:TABLE_OFFSET + len(table)] = table
    for part in old:
        if part.type == 1:
            backup[part.offset:part.end] = bytes((part.offset // 4096 % 251,)) * part.size
    output = tmp_path / "plan"
    output.mkdir()
    plan = prepare(bytes(backup), parts, files, output)
    assert {item["offset"] for item in plan["images"]} == {"0x0", "0x8000", "0x5a000", "0x60000"}
    assert {p["name"] for p in plan["preserved"]} == {"nvs", "phy_init", "icon_store"}
    for part in old:
        if part.type == 1:
            assert (output / f"{part.name}-preserved.bin").read_bytes() == backup[part.offset:part.end]


def test_usb_recovery_resets_ota_boot_metadata(tmp_path, firmware_blob):
    _, parts, files = ota_build(tmp_path, firmware_blob)
    backup = bytearray(b"\xff" * 0xE60000)
    table = b"".join(entry(p.name, p.type, p.subtype, p.offset, p.size) for p in parts)
    backup[TABLE_OFFSET:TABLE_OFFSET + len(table)] = table
    backup[OTA_DATA_OFFSET:OTA_DATA_OFFSET + 8192] = b"old OTA boot metadata".ljust(8192, b"\0")
    output = tmp_path / "recovery"
    output.mkdir()
    plan = prepare(bytes(backup), parts, files, output)
    metadata = next(item for item in plan["images"] if item["offset"] == hex(OTA_DATA_OFFSET))
    assert files[OTA_DATA_OFFSET].read_bytes() == b"\xff" * 8192
    assert metadata["size"] == 8192
    assert not any(item["offset"] in ("0x660000", "0xa60000") for item in plan["images"])


def test_invalid_ota_metadata_is_rejected_before_flash(tmp_path, firmware_blob):
    ota_build(tmp_path, firmware_blob)
    (tmp_path / "ota_data_initial.bin").write_bytes(bytes(8192))
    with pytest.raises(ValueError, match="metadata must be erased"):
        load_build(tmp_path)


def test_physical_flash_capacity_must_be_detected():
    assert detected_flash_size("Detected flash size: 16MB\n") == 16 * 1024 * 1024
    assert detected_flash_size("Detected flash size: 8 MB\n") == 8 * 1024 * 1024
    with pytest.raises(ValueError, match="physical flash capacity"):
        detected_flash_size("No flash chip detected")


def test_only_read_commands_are_retried(tmp_path, monkeypatch):
    replies = iter([SimpleNamespace(returncode=1, stdout="Corrupt data", stderr=""),
                    SimpleNamespace(returncode=0, stdout="Read verified", stderr="")])
    calls = []
    def execute(command, **_kwargs):
        calls.append(command)
        return next(replies)
    monkeypatch.setattr("tools.flash_firmware.subprocess.run", execute)
    assert run_esptool("COM17", ["read_flash", "0", "0x1000", "backup.bin"], tmp_path, "read") == "Read verified"
    assert len(calls) == 2 and calls[0] == calls[1]
    calls.clear()
    monkeypatch.setattr("tools.flash_firmware.subprocess.run",
                        lambda command, **kw: calls.append(command) or SimpleNamespace(returncode=1, stdout="Failed", stderr=""))
    with pytest.raises(RuntimeError):
        run_esptool("COM17", ["write_flash", "0", "image.bin"], tmp_path, "write")
    assert len(calls) == 1


def test_backup_joins_only_complete_verified_blocks(tmp_path, monkeypatch):
    expected = bytes(range(256)) * 5000
    operations = []
    def read(_port, arguments, _output, _name, **kwargs):
        offset, size = int(arguments[1], 0), int(arguments[2], 0)
        operations.append((offset, size))
        from pathlib import Path
        Path(arguments[3]).write_bytes(expected[offset:offset + size])
        return "Read verified"
    monkeypatch.setattr("tools.flash_firmware.run_esptool", read)
    assert backup_flash("COM17", len(expected), tmp_path) == expected
    assert operations == [(0, 524288), (524288, 524288), (1048576, len(expected) - 1048576)]
    assert (tmp_path / "before-flash.bin").read_bytes() == expected
