"""Reject incompatible, damaged and incomplete firmware before any BLE write."""
import hashlib
import struct

import pytest

from companion.firmware import FirmwareError, FirmwareImage


def test_valid_image_snapshot_and_descriptor(firmware_blob, tmp_path):
    source = tmp_path / "nexus_deck.bin"
    source.write_bytes(firmware_blob)
    image = FirmwareImage.load(source)
    assert image.path == source.resolve()
    assert image.version == "1.3.0"
    assert image.sha256 == hashlib.sha256(firmware_blob).hexdigest()
    assert image.elf_sha256 == bytes(range(32)).hex()
    source.write_bytes(b"changed after validation")
    assert image.data == firmware_blob


@pytest.mark.parametrize("damage,expected", [
    ("chip", "ESP32-S3"), ("project", "NEXUS DECK"),
    ("version", "NEXUS DECK"), ("descriptor", "descriptor"),
    ("length", "segment"), ("payload", "checksum"),
    ("digest", "SHA-256"), ("truncate", "SHA-256"),
    ("trailer", "SHA-256"), ("magic", "application"),
])
def test_rejects_damaged_images(firmware_blob, damage, expected):
    data = bytearray(firmware_blob)
    if damage == "chip":
        struct.pack_into("<H", data, 12, 0)
    elif damage == "project":
        data[80:90] = b"other_deck"
    elif damage == "version":
        data[48] = ord("/")
    elif damage == "descriptor":
        data[32] ^= 1
    elif damage == "length":
        struct.pack_into("<I", data, 28, len(data) * 2)
    elif damage == "payload":
        data[400] ^= 1
    elif damage == "digest":
        data[-1] ^= 1
    elif damage == "truncate":
        del data[-10:]
    elif damage == "trailer":
        data += b"extra"
    elif damage == "magic":
        data[0] = 0
    with pytest.raises(FirmwareError, match=expected):
        FirmwareImage.parse(bytes(data))


def test_rejects_bootloader_merged_and_oversized_files(firmware_blob, tmp_path):
    for name, data in (("bootloader.bin", bytes(1000)),
                       ("flash.bin", bytes(0x60000) + firmware_blob),
                       ("nexus_deck.txt", firmware_blob),
                       ("too-large.bin", bytes(4 * 1024 * 1024 + 1))):
        source = tmp_path / name
        source.write_bytes(data)
        with pytest.raises(FirmwareError):
            FirmwareImage.load(source)
