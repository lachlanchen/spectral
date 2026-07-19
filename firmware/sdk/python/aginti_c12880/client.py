"""Thread-free serial client suitable for scripts and acquisition services."""

from __future__ import annotations

import struct
import time
from typing import Iterator

import serial

from .protocol import (
    OP_CONFIGURE,
    OP_SINGLE_SHOT,
    OP_START_STREAM,
    OP_STOP_STREAM,
    StreamDecoder,
    SpectrumFrame,
    command,
)


class SpectrometerClient:
    def __init__(self, port: str, timeout: float = 0.25) -> None:
        self.port = port
        self.timeout = timeout
        self.serial: serial.Serial | None = None
        self.sequence = 1
        self.decoder = StreamDecoder()

    def __enter__(self) -> "SpectrometerClient":
        self.open()
        return self

    def __exit__(self, *_: object) -> None:
        self.close()

    def open(self) -> None:
        if self.serial is None:
            self.serial = serial.Serial(self.port, 1_500_000, timeout=self.timeout)
            self.serial.reset_input_buffer()

    def close(self) -> None:
        if self.serial is not None:
            self.serial.close()
            self.serial = None

    def _write_command(self, opcode: int, payload: bytes = b"") -> int:
        if self.serial is None:
            raise RuntimeError("client is not open")
        sequence = self.sequence
        self.sequence = (self.sequence + 1) & 0xFFFFFFFF
        self.serial.write(command(opcode, sequence, payload))
        return sequence

    def identity(self) -> bytes:
        if self.serial is None:
            raise RuntimeError("client is not open")
        self.serial.write(b"\x04\r\n")
        reply = self.serial.read(8)
        if len(reply) != 8 or reply[2:] != b"c12880":
            raise RuntimeError(f"unexpected identity response: {reply!r}")
        return reply

    def configure(self, clock_hz: int, exposure_clocks: int,
                  output_mode: int = 0, frame_format: int = 1) -> None:
        payload = struct.pack("<IIBB2x", clock_hz, exposure_clocks,
                              output_mode, frame_format)
        self._write_command(OP_CONFIGURE, payload)

    def single(self) -> None:
        self._write_command(OP_SINGLE_SHOT)

    def start(self) -> None:
        self._write_command(OP_START_STREAM)

    def stop(self) -> None:
        self._write_command(OP_STOP_STREAM)

    def frames(self, duration: float | None = None) -> Iterator[SpectrumFrame]:
        if self.serial is None:
            raise RuntimeError("client is not open")
        deadline = None if duration is None else time.monotonic() + duration
        while deadline is None or time.monotonic() < deadline:
            data = self.serial.read(self.serial.in_waiting or 64)
            for frame in self.decoder.feed(data):
                yield frame

