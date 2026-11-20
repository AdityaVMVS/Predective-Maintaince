"""Decode the monitor's CRC-protected feature and capture lines."""
from dataclasses import dataclass
import struct
import zlib

FEATURES = ("rms_g", "peak_g", "crest", "kurtosis", "dominant_hz", "energy_g2",
            "centroid_hz", "low_g2", "mid_g2", "high_g2")
COLUMNS = ("sequence", "timestamp_ms", "state", "cycles", "spi_errors", "overruns",
           "dropped", "deadline_misses", "trained", "axis", "score", *FEATURES)
STATES = {"LEARNING", "NORMAL", "WARNING", "CRITICAL", "ANOMALY",
          "SENSOR_FAULT", "ACQUISITION_FAULT"}
WINDOW = 1024
RATE = 1600
MAX_LINE = 251


@dataclass(frozen=True)
class RawChunk:
    capture: int
    offset: int
    samples: tuple


def decode(line):
    body, checksum = line.rstrip(b"\r\n").rsplit(b"*", 1)
    if len(checksum) != 8 or int(checksum, 16) != zlib.crc32(body):
        raise ValueError("telemetry checksum mismatch")
    kind, *values = body.decode("ascii").split(",")
    if kind == "F" and len(values) == len(COLUMNS):
        values[:2] = map(int, values[:2])
        values[3:10] = map(int, values[3:10])
        if (values[2] not in STATES or any(not 0 <= n <= 0xffffffff for n in
                                          (*values[:2], *values[3:8]))
                or not 0 <= values[8] <= 64 or not 0 <= values[9] <= 2):
            raise ValueError("invalid feature field")
        values[10:] = [int(v) / 1e6 for v in values[10:]]
        return dict(zip(COLUMNS, values))
    if kind == "R" and len(values) == 3:
        capture, offset = map(int, values[:2])
        raw = bytes.fromhex(values[2])
        if not 0 <= capture <= 0xffffffff or not 0 <= offset < WINDOW or offset % 8 or len(raw) != 48:
            raise ValueError("invalid raw chunk")
        return RawChunk(capture, offset, tuple(struct.iter_unpack("<hhh", raw)))
    raise ValueError("unknown or incomplete telemetry record")


class Stream:
    def __init__(self):
        self.buffer = bytearray()
        self.discard = False
        self.bad = 0
        self.gaps = 0
        self.last = None

    def feed(self, data):
        for byte in data:
            if byte == 10:
                if self.buffer and not self.discard:
                    try:
                        record = decode(self.buffer)
                    except ValueError:
                        self.bad += 1
                    else:
                        if isinstance(record, dict):
                            current = record["sequence"]
                            if self.last is not None:
                                distance = (current - self.last) & 0xffffffff
                                if 1 < distance < 0x80000000:
                                    self.gaps += distance - 1
                            self.last = current
                        yield record
                self.buffer.clear()
                self.discard = False
            elif not self.discard:
                self.buffer.append(byte)
                if len(self.buffer) > MAX_LINE:
                    self.bad += 1
                    self.buffer.clear()
                    self.discard = True


class Capture:
    def __init__(self):
        self.number = None
        self.samples = []

    def add(self, chunk):
        if chunk.offset == 0:
            self.number = chunk.capture
            self.samples = []
        if chunk.capture != self.number or chunk.offset != len(self.samples):
            self.number = None
            self.samples = []
            return None
        self.samples.extend(chunk.samples)
        if len(self.samples) == WINDOW:
            finished = self.samples
            self.samples = []
            self.number = None
            return finished
        return None
