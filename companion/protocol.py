"""Bounded, CRC-protected framing shared with the ESP32 implementation."""
from __future__ import annotations

import json
import math
import struct
import time
import zlib
from collections.abc import Mapping
from typing import Any

SERVICE_UUID = "b7c20000-7b9b-4e7d-9e10-a4f68c120001"
COMMAND_UUID = "b7c20001-7b9b-4e7d-9e10-a4f68c120001"
TELEMETRY_UUID = "b7c20002-7b9b-4e7d-9e10-a4f68c120001"
RESPONSE_UUID = "b7c20003-7b9b-4e7d-9e10-a4f68c120001"
PROFILE_UUID = "b7c20004-7b9b-4e7d-9e10-a4f68c120001"
STATUS_UUID = "b7c20005-7b9b-4e7d-9e10-a4f68c120001"
FIRMWARE_UUID = "b7c20006-7b9b-4e7d-9e10-a4f68c120001"
VERSION = 1
MAX_MESSAGE = 49152
HEADER = struct.Struct("<2sBBHHHHI")
HEADER_SIZE = HEADER.size
ASSEMBLY_TIMEOUT = 10.0


class ProtocolError(ValueError):
    """An invalid message must never reach the action engine."""


def _reject_duplicates(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ProtocolError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def _depth(value: Any, level: int = 0) -> None:
    if level > 12:
        raise ProtocolError("JSON nesting is too deep")
    if isinstance(value, dict):
        if len(value) > 32:
            raise ProtocolError("Too many fields")
        for key, item in value.items():
            if not isinstance(key, str) or len(key) > 64:
                raise ProtocolError("Invalid field name")
            _depth(item, level + 1)
    elif isinstance(value, list):
        if len(value) > 256:
            raise ProtocolError("Array is too long")
        for item in value:
            _depth(item, level + 1)
    elif isinstance(value, float) and not math.isfinite(value):
        raise ProtocolError("Non-finite numbers are forbidden")


def encode_json(message: Mapping[str, Any]) -> bytes:
    _depth(message)
    try:
        payload = json.dumps(message, ensure_ascii=False, separators=(",", ":"),
                             allow_nan=False).encode("utf-8")
    except (ValueError, TypeError, UnicodeError) as exc:
        raise ProtocolError(str(exc)) from exc
    if not 1 <= len(payload) <= MAX_MESSAGE:
        raise ProtocolError(f"Message exceeds {MAX_MESSAGE} bytes")
    return payload


def decode_json(payload: bytes, *, max_bytes: int = MAX_MESSAGE) -> dict[str, Any]:
    if not 1 <= len(payload) <= max_bytes or b"\x00" in payload:
        raise ProtocolError("Invalid JSON payload length or NUL")
    try:
        obj = json.loads(payload.decode("utf-8"), object_pairs_hook=_reject_duplicates,
                         parse_constant=lambda s: (_ for _ in ()).throw(ProtocolError(s)))
    except (ValueError, UnicodeError, RecursionError) as exc:
        raise ProtocolError(f"Invalid JSON: {exc}") from exc
    if not isinstance(obj, dict):
        raise ProtocolError("Message must be an object")
    _depth(obj)
    return obj


def sequence(value: Any) -> int:
    if type(value) is not int or not 1 <= value <= 0xFFFFFFFF:
        raise ProtocolError("sequence must be a nonzero uint32")
    return value


def fragment(payload: bytes, message_id: int, att_payload: int = 20) -> list[bytes]:
    if not 1 <= len(payload) <= MAX_MESSAGE:
        raise ProtocolError("Invalid message length")
    if not 1 <= message_id <= 65535 or not HEADER_SIZE < att_payload <= 512:
        raise ProtocolError("Invalid message ID or ATT payload size")
    checksum = zlib.crc32(payload) & 0xFFFFFFFF
    chunk_size = att_payload - HEADER_SIZE
    return [HEADER.pack(b"ND", VERSION, 0, message_id, len(payload), offset,
                        len(payload[offset:offset + chunk_size]), checksum)
            + payload[offset:offset + chunk_size]
            for offset in range(0, len(payload), chunk_size)]


class Reassembler:
    """One ordered message per characteristic; exact packet retries are harmless."""

    def __init__(self, capacity: int = MAX_MESSAGE, timeout: float = ASSEMBLY_TIMEOUT):
        self.capacity, self.timeout = capacity, timeout
        self.reset()

    def reset(self) -> None:
        self.message_id = self.total = self.checksum = 0
        self.buffer = bytearray()
        self.last_packet = 0.0

    def feed(self, packet: bytes, now: float | None = None) -> bytes | None:
        now = time.monotonic() if now is None else now
        try:
            if not HEADER_SIZE < len(packet) <= 512:
                raise ProtocolError("Truncated or oversized frame")
            magic, version, flags, msg_id, total, offset, length, crc = HEADER.unpack_from(packet)
            if (magic != b"ND" or version != VERSION or flags != 0 or msg_id == 0
                    or not 1 <= total <= self.capacity or length != len(packet) - HEADER_SIZE
                    or offset + length > total):
                raise ProtocolError("Invalid frame header")
            if self.buffer and now - self.last_packet > self.timeout:
                self.reset()
            chunk = packet[HEADER_SIZE:]
            if offset == 0 and (msg_id != self.message_id or not self.buffer):
                self.message_id, self.total, self.checksum = msg_id, total, crc
                self.buffer = bytearray()
            if (msg_id, total, crc) != (self.message_id, self.total, self.checksum):
                raise ProtocolError("Interleaved fragments")
            if offset < len(self.buffer):
                if bytes(self.buffer[offset:offset + length]) != chunk:
                    raise ProtocolError("Conflicting retry")
                self.last_packet = now
                return None
            if offset != len(self.buffer):
                raise ProtocolError("Missing or out-of-order fragment")
            self.buffer.extend(chunk)
            self.last_packet = now
            if len(self.buffer) != total:
                return None
            payload = bytes(self.buffer)
            if zlib.crc32(payload) & 0xFFFFFFFF != crc:
                raise ProtocolError("CRC mismatch")
            # Keep the completed frame for exact final-packet retries. A new message
            # ID starts the next assembly; callers receive a payload only once.
            return payload
        except ProtocolError:
            self.reset()
            raise
