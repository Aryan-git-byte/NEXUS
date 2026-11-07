"""Validate an ESP-IDF application before offering it to an authorized deck."""
from __future__ import annotations

import hashlib
import re
import struct
from dataclasses import dataclass
from pathlib import Path


class FirmwareError(ValueError):
    """A file or update result is incompatible with NEXUS DECK."""


@dataclass(frozen=True, slots=True)
class FirmwareImage:
    path: Path
    data: bytes
    version: str
    sha256: str
    elf_sha256: str

    @classmethod
    def load(cls, path: str | Path) -> "FirmwareImage":
        source = Path(path).resolve(strict=True)
        if source.suffix.lower() != ".bin" or not 512 <= source.stat().st_size <= 4 * 1024 * 1024:
            raise FirmwareError("Select nexus_deck.bin, between 512 bytes and 4 MB")
        return cls.parse(source.read_bytes(), source)

    @classmethod
    def parse(cls, data: bytes, path: Path = Path("nexus_deck.bin")) -> "FirmwareImage":
        if not 512 <= len(data) <= 4 * 1024 * 1024 or data[0] != 0xE9:
            raise FirmwareError("Select an ESP32-S3 application .bin, not a merged flash image")
        if struct.unpack_from("<H", data, 12)[0] != 9 or not 1 <= data[1] <= 16:
            raise FirmwareError("Firmware does not target ESP32-S3")
        if struct.unpack_from("<I", data, 32)[0] != 0xABCD5432:
            raise FirmwareError("The file has no ESP-IDF application descriptor")
        def field(offset: int) -> str:
            raw = data[offset:offset + 32]
            if b"\0" not in raw:
                raise FirmwareError("Invalid application descriptor")
            try:
                return raw.split(b"\0", 1)[0].decode("ascii")
            except UnicodeError as exc:
                raise FirmwareError("Invalid application descriptor") from exc
        version, project = field(48), field(80)
        if project != "nexus_deck" or not re.fullmatch(r"[A-Za-z0-9._-]{1,31}", version):
            raise FirmwareError("Select firmware built for NEXUS DECK")
        position, checksum = 24, 0xEF
        for _ in range(data[1]):
            if position + 8 > len(data):
                raise FirmwareError("Firmware segment header is truncated")
            length = struct.unpack_from("<I", data, position + 4)[0]
            position += 8
            if length == 0 or position + length > len(data):
                raise FirmwareError("Firmware segment is truncated or invalid")
            for value in data[position:position + length]:
                checksum ^= value
            position += length
        checksum_offset = position | 15
        if checksum_offset >= len(data) or data[checksum_offset] != checksum:
            raise FirmwareError("Firmware checksum failed")
        end = checksum_offset + 1
        if data[23] == 1:
            if len(data) != end + 32 or hashlib.sha256(data[:end]).digest() != data[end:]:
                raise FirmwareError("Firmware image SHA-256 failed")
        elif data[23] != 0 or len(data) != end:
            raise FirmwareError("Unsupported firmware trailer")
        return cls(path, bytes(data), version, hashlib.sha256(data).hexdigest(), data[176:208].hex())
