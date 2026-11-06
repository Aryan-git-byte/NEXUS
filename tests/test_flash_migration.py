"""Verify calibration-preserving partition expansion against corrupted inputs."""
import hashlib
import struct

import pytest
from tools.flash_firmware import Partition, migrated_partition, parse_table


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
