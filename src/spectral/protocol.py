"""Wire protocol and frame validation for the vendor C12880MA controller."""

from __future__ import annotations

from dataclasses import dataclass
import time

import numpy as np

PIXEL_COUNT = 288
HEADER_BYTES = 12
PIXEL_BYTES = PIXEL_COUNT * 2
TRAILER_BYTES = 2
FRAME_BYTES = HEADER_BYTES + PIXEL_BYTES + TRAILER_BYTES
PIXEL_DATA_END = HEADER_BYTES + PIXEL_BYTES
PREFIX_WORDS = HEADER_BYTES // 2

IDENTITY_BAUD_RATE = 256_000
BAUD_RATE = 1_500_000
IDENTITY_REQUEST = b"\x04\r\n"
IDENTITY_PAYLOAD = b"c12880"
IDENTITY_REPLY_BYTES = 8
CORRECTION_REQUEST = b"\xff\x09"
CORRECTION_BYTES = 1_024

ADC_MAX = 65_535
DEFAULT_EXPOSURE_MS = 0.010
MIN_EXPOSURE_MS = 0.003
MAX_EXPOSURE_MS = 10_000.0
TICKS_PER_MS = 5_000
DEFAULT_OUTPUT_MASK = 0x01


class ProtocolError(RuntimeError):
    """Base class for malformed or incomplete controller responses."""


class ShortFrameError(ProtocolError):
    """The controller returned fewer bytes than one complete frame."""


class ZeroFrameError(ProtocolError):
    """The transport returned a complete but entirely zero-filled frame."""


@dataclass(frozen=True, slots=True)
class SpectrumFrame:
    sequence: int
    timestamp_ns: int
    exposure_ms: float
    counts: np.ndarray
    prefix_words: tuple[int, ...]
    raw_size: int
    source: str

    @property
    def saturated_pixels(self) -> int:
        return int(np.count_nonzero(self.counts >= ADC_MAX))


def exposure_ticks(exposure_ms: float) -> int:
    value = float(exposure_ms)
    if not MIN_EXPOSURE_MS <= value <= MAX_EXPOSURE_MS:
        raise ValueError(
            f"Exposure must be {MIN_EXPOSURE_MS:g}-{MAX_EXPOSURE_MS:g} ms"
        )
    return int(round(value * TICKS_PER_MS))


def build_command(
    command: int,
    exposure_ms: float,
    output_mask: int = DEFAULT_OUTPUT_MASK,
    *,
    terminated: bool = False,
) -> bytes:
    if not 0 <= command <= 0xFF:
        raise ValueError("command must fit in one byte")
    if not 0 <= output_mask <= 0xFF:
        raise ValueError("output_mask must fit in one byte")
    ticks = exposure_ticks(exposure_ms)
    packet = bytes((0xFF, command, output_mask)) + ticks.to_bytes(4, "big")
    return packet + (b"\r\n" if terminated else b"")


def build_read_command(
    exposure_ms: float, output_mask: int = DEFAULT_OUTPUT_MASK
) -> bytes:
    # Fresh vendor sessions use a nine-byte CR/LF-terminated request. The
    # controller also accepts seven-byte requests after some UI updates, but
    # the terminated form works from startup and avoids state dependence.
    return build_command(0xAA, exposure_ms, output_mask, terminated=True)


def build_exposure_command(
    exposure_ms: float, output_mask: int = DEFAULT_OUTPUT_MASK
) -> bytes:
    # The vendor worker terminates explicit FF FF updates with CR/LF.
    return build_command(0xFF, exposure_ms, output_mask, terminated=True)


def identity_is_valid(raw: bytes) -> bool:
    return len(raw) >= IDENTITY_REPLY_BYTES and raw[2:8] == IDENTITY_PAYLOAD


def decode_frame(
    raw: bytes,
    *,
    sequence: int,
    exposure_ms: float,
    source: str,
) -> SpectrumFrame:
    if len(raw) < FRAME_BYTES:
        zero_note = " (all received bytes were zero)" if raw and not any(raw) else ""
        raise ShortFrameError(
            f"Expected {FRAME_BYTES} bytes, received {len(raw)}{zero_note}"
        )

    frame = memoryview(raw)[:FRAME_BYTES]
    prefix = np.frombuffer(frame[:HEADER_BYTES], dtype="<u2", count=PREFIX_WORDS)
    counts = np.frombuffer(
        frame[HEADER_BYTES:PIXEL_DATA_END], dtype="<u2", count=PIXEL_COUNT
    ).astype(np.float64, copy=True)
    if counts.size != PIXEL_COUNT:
        raise ProtocolError(f"Decoded {counts.size} pixels instead of {PIXEL_COUNT}")
    if not np.any(counts):
        raise ZeroFrameError(
            "Received a complete zero-filled frame; controller initialization did not complete"
        )

    return SpectrumFrame(
        sequence=sequence,
        timestamp_ns=time.time_ns(),
        exposure_ms=float(exposure_ms),
        counts=counts,
        prefix_words=tuple(int(value) for value in prefix),
        raw_size=len(raw),
        source=source,
    )
