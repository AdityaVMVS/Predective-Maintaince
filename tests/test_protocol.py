import struct
import zlib

import pytest

from host.protocol import Capture, Stream, decode


def wire(body):
    encoded = body.encode("ascii")
    return encoded + f"*{zlib.crc32(encoded):08x}\n".encode("ascii")


def feature_fields(sequence=0):
    return ["F", str(sequence), "123", "NORMAL", "12000", "1", "2", "3", "4", "64", "2",
            *(["200000"] * 11)]


@pytest.mark.parametrize("index,replacement", [
    (0, "Q"), (1, "-1"), (2, "-1"), (3, "UNKNOWN"), (4, "bad"), (5, "-2"),
    (9, "65"), (10, "3"), (11, "abc"),
])
def test_reject_invalid_feature(index, replacement):
    body = feature_fields()
    body[index] = replacement
    with pytest.raises(ValueError):
        decode(wire(",".join(body)))


def test_crc_and_sequence_wrap():
    stream = Stream()
    frame = wire(",".join(feature_fields(0xffffffff)))
    assert not list(stream.feed(frame[:8]))
    assert list(stream.feed(frame[8:]))[0]["rms_g"] == 0.2
    frame = wire(",".join(feature_fields(1)))
    bad = bytearray(frame)
    bad[-3] ^= 1
    assert not list(stream.feed(bad))
    assert list(stream.feed(frame))[0]["sequence"] == 1
    assert stream.bad == 1 and stream.gaps == 1


def chunk(offset, capture=1):
    return decode(wire(f"R,{capture},{offset}," + (struct.pack("<hhh", -130, 25, 5) * 8).hex()))


def test_complete_capture():
    capture = Capture()
    for offset in range(0, 1016, 8):
        assert capture.add(chunk(offset)) is None
    data = capture.add(chunk(1016))
    assert data == [(-130, 25, 5)] * 1024


def test_missing_capture_chunk():
    capture = Capture()
    capture.add(chunk(0))
    for offset in range(16, 1024, 8):
        assert capture.add(chunk(offset)) is None


def test_recover_from_oversized_input():
    stream = Stream()
    data = b"a" * 500 + b"\n" + wire(",".join(feature_fields()))
    assert len(list(stream.feed(data))) == 1
    assert stream.bad == 1


@pytest.mark.parametrize("body", ["R,0,-8,abcd", "R,1,3,abcd", "R,2,8,abcd", "R,2,1024,abcd"])
def test_invalid_capture(body):
    with pytest.raises(ValueError):
        decode(wire(body))
