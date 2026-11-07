"""Deterministic ESP-IDF images for validation and simulated transport tests."""
import hashlib
import struct

import pytest


@pytest.fixture
def firmware_blob():
    image = bytearray(24 + 8 + 8192)
    image[0:4] = bytes((0xE9, 1, 2, 0x4F))
    struct.pack_into("<H", image, 12, 9)
    image[23] = 1
    struct.pack_into("<II", image, 24, 0x3C000020, 8192)
    struct.pack_into("<I", image, 32, 0xABCD5432)
    image[48:53] = b"1.3.0"
    image[80:90] = b"nexus_deck"
    image[176:208] = bytes(range(32))
    for position in range(288, len(image)):
        image[position] = position % 251
    checksum = 0xEF
    for value in image[32:]:
        checksum ^= value
    image += bytes((len(image) | 15) - len(image)) + bytes((checksum,))
    return bytes(image) + hashlib.sha256(image).digest()
