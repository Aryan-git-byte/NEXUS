import json
import random
import struct
import zlib

import pytest

from companion.protocol import (HEADER, MAX_MESSAGE, ProtocolError, Reassembler,
                                decode_json, encode_json, fragment, sequence)


@pytest.mark.parametrize("att", [20, 61, 182, 244, 509, 512])
@pytest.mark.parametrize("size", [1, 4, 29, 160, 3478, MAX_MESSAGE])
def test_fragment_roundtrip(att, size):
    payload = random.Random(size).randbytes(size)
    packets = fragment(payload, 123, att)
    rx = Reassembler()
    results = [rx.feed(packet, now=1 + index * 0.0001) for index, packet in enumerate(packets)]
    assert results[-1] == payload
    assert all(value is None for value in results[:-1])
    assert all(len(packet) <= att for packet in packets)


def test_wire_golden_vector():
    payload = b'{"type":"action","id":"vscode","sequence":12}'
    frames = fragment(payload, 0x1234, 61)
    assert frames[0][:16].hex() == "4e44010034122d0000002d00c87965c3"
    assert HEADER.size == 16


def test_exact_fragment_retry_is_safe():
    packets = fragment(b"x" * 200, 7, 61)
    rx = Reassembler()
    assert rx.feed(packets[0]) is None
    assert rx.feed(packets[0]) is None
    assert [rx.feed(p) for p in packets[1:]][-1] == b"x" * 200
    assert rx.feed(packets[-1]) is None
    assert all(rx.feed(p) is None for p in packets)
    assert rx.feed(fragment(b"next", 8, 61)[0]) == b"next"


def test_corrupt_crc_and_recovery():
    packet = bytearray(fragment(b"safe", 1, 61)[0])
    packet[-1] ^= 1
    rx = Reassembler()
    with pytest.raises(ProtocolError, match="CRC"):
        rx.feed(packet)
    assert rx.feed(fragment(b"good", 2, 61)[0]) == b"good"


def test_out_of_order_and_timeout():
    packets = fragment(b"x" * 200, 1, 61)
    rx = Reassembler()
    with pytest.raises(ProtocolError):
        rx.feed(packets[1])
    rx.feed(packets[0], now=0)
    with pytest.raises(ProtocolError):
        rx.feed(packets[1], now=11)


@pytest.mark.parametrize("payload", [b"[]", b"{", b'{"type":"a","type":"b"}',
                                      b'{"cpu":NaN}', b"\xff", b"{}\x00", b"{} garbage"])
def test_bad_json(payload):
    with pytest.raises(ProtocolError):
        decode_json(payload)


def test_depth_and_message_limits():
    with pytest.raises(ProtocolError):
        decode_json(b'{"a":' + b"[" * 14 + b"0" + b"]" * 14 + b"}")
    with pytest.raises(ProtocolError):
        encode_json({"a": "x" * MAX_MESSAGE})
    with pytest.raises(ProtocolError):
        fragment(b"x", 0)


@pytest.mark.parametrize("value", [0, -1, 2**32, True, 1.5, "12", None])
def test_sequence_validation(value):
    with pytest.raises(ProtocolError):
        sequence(value)


def test_random_invalid_headers_are_bounded():
    rng = random.Random(73)
    rx = Reassembler()
    for _ in range(300):
        packet = rng.randbytes(rng.randrange(1, 520))
        try:
            assert rx.feed(packet) is None
        except ProtocolError:
            assert len(rx.buffer) == 0
