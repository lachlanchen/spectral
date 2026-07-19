"""Binary protocol codecs with strict length, sequence, and CRC checks."""

from __future__ import annotations

from dataclasses import dataclass
import struct
import zlib

import numpy as np

COMMAND = struct.Struct("<4sBBHIHHI")
FRAME = struct.Struct("<4sBBHIIQIIHHIII")
REPLY = struct.Struct("<4sBBHIHHI")

OP_HELLO = 0x01
OP_GET_CAPS = 0x02
OP_CONFIGURE = 0x10
OP_SINGLE_SHOT = 0x11
OP_START_STREAM = 0x12
OP_STOP_STREAM = 0x13
OP_GET_STATUS = 0x14
OP_EEPROM_READ = 0x20


def _crc_record(record: bytes, crc_offset: int) -> int:
    mutable = bytearray(record)
    mutable[crc_offset : crc_offset + 4] = b"\0\0\0\0"
    return zlib.crc32(mutable) & 0xFFFFFFFF


def command(opcode: int, sequence: int, payload: bytes = b"", flags: int = 0) -> bytes:
    if len(payload) > 64:
        raise ValueError("command payload exceeds 64 bytes")
    header = COMMAND.pack(b"ASC2", 2, opcode, flags, sequence, len(payload), 0, 0)
    record = header + payload
    crc = _crc_record(record, 16)
    return record[:16] + struct.pack("<I", crc) + record[20:]


@dataclass(frozen=True, slots=True)
class SpectrumFrame:
    sequence: int
    timestamp_us: int
    exposure_clocks: int
    sensor_clock_hz: int
    status: int
    dropped_frames: int
    samples: np.ndarray

    @property
    def pixels(self) -> np.ndarray:
        return self.samples[:288]


def decode_frame(record: bytes) -> SpectrumFrame:
    if len(record) < FRAME.size:
        raise ValueError("short frame header")
    fields = FRAME.unpack_from(record)
    magic, version, header_bytes = fields[:3]
    if magic != b"ASP2" or version != 2 or header_bytes != FRAME.size:
        raise ValueError("invalid frame identity")
    payload_bytes = fields[5]
    total = header_bytes + payload_bytes
    if len(record) != total:
        raise ValueError(f"frame length {len(record)} != declared {total}")
    if _crc_record(record, 44) != fields[13]:
        raise ValueError("frame CRC mismatch")
    samples = np.frombuffer(record, dtype="<u2", count=fields[10], offset=header_bytes).copy()
    return SpectrumFrame(
        sequence=fields[4],
        timestamp_us=fields[6],
        exposure_clocks=fields[7],
        sensor_clock_hz=fields[8],
        status=fields[11],
        dropped_frames=fields[12],
        samples=samples,
    )


class StreamDecoder:
    """Incremental decoder that resynchronizes after noise or a dropped USB read."""

    def __init__(self) -> None:
        self.buffer = bytearray()
        self.crc_failures = 0
        self.discarded_bytes = 0

    def feed(self, data: bytes) -> list[SpectrumFrame]:
        self.buffer.extend(data)
        frames: list[SpectrumFrame] = []
        while True:
            start = self.buffer.find(b"ASP2")
            if start < 0:
                keep = min(3, len(self.buffer))
                self.discarded_bytes += len(self.buffer) - keep
                if keep:
                    del self.buffer[:-keep]
                else:
                    self.buffer.clear()
                break
            if start:
                self.discarded_bytes += start
                del self.buffer[:start]
            if len(self.buffer) < FRAME.size:
                break
            header = FRAME.unpack_from(self.buffer)
            total = header[2] + header[5]
            if header[2] != FRAME.size or total > 4096:
                del self.buffer[0]
                self.discarded_bytes += 1
                continue
            if len(self.buffer) < total:
                break
            record = bytes(self.buffer[:total])
            del self.buffer[:total]
            try:
                frames.append(decode_frame(record))
            except ValueError:
                self.crc_failures += 1
        return frames
