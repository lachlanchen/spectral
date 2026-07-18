"""Vendor-compatible discovery, initialization, and frame acquisition."""

from __future__ import annotations

from dataclasses import asdict, dataclass
import threading
import time
from typing import Iterable, Literal

import numpy as np
import serial
from serial.tools import list_ports

from .protocol import (
    BAUD_RATE,
    CORRECTION_BYTES,
    CORRECTION_REQUEST,
    DEFAULT_EXPOSURE_MS,
    DEFAULT_OUTPUT_MASK,
    FRAME_BYTES,
    IDENTITY_BAUD_RATE,
    IDENTITY_REPLY_BYTES,
    IDENTITY_REQUEST,
    ShortFrameError,
    SpectrumFrame,
    ZeroFrameError,
    build_exposure_command,
    build_read_command,
    decode_frame,
    exposure_ticks,
    identity_is_valid,
)

CH343_VID = 0x1A86
CH343_PID = 0x55D3
C12880_VID = 0x0483
C12880_PID = 0x5740
TriggerMode = Literal["internal", "external"]


class DeviceError(RuntimeError):
    """Controller discovery or communication failed."""


@dataclass(frozen=True, slots=True)
class PortDescriptor:
    device: str
    description: str
    manufacturer: str | None
    vid: int | None
    pid: int | None
    serial_number: str | None
    score: int

    def to_dict(self) -> dict[str, object]:
        payload = asdict(self)
        payload["vid_pid"] = (
            f"{self.vid:04X}:{self.pid:04X}"
            if self.vid is not None and self.pid is not None
            else None
        )
        return payload


def _score_port(port: object) -> int:
    score = 0
    if (
        getattr(port, "vid", None) == C12880_VID
        and getattr(port, "pid", None) == C12880_PID
    ):
        score += 300
    if getattr(port, "vid", None) == CH343_VID:
        score += 60
    if getattr(port, "pid", None) == CH343_PID:
        score += 60
    text = " ".join(
        str(value or "")
        for value in (
            getattr(port, "description", ""),
            getattr(port, "manufacturer", ""),
            getattr(port, "product", ""),
        )
    ).lower()
    if "ch343" in text:
        score += 80
    if "usb-enhanced-serial" in text:
        score += 20
    if "esp32" in text or "jtag" in text:
        score -= 100
    return score


def enumerate_ports() -> list[PortDescriptor]:
    result = []
    for port in list_ports.comports():
        result.append(
            PortDescriptor(
                device=port.device,
                description=port.description or "Unknown serial device",
                manufacturer=port.manufacturer,
                vid=port.vid,
                pid=port.pid,
                serial_number=port.serial_number,
                score=_score_port(port),
            )
        )
    return sorted(result, key=lambda item: (-item.score, item.device))


def choose_port(requested: str | None = None) -> PortDescriptor:
    ports = enumerate_ports()
    if requested:
        for port in ports:
            if port.device.casefold() == requested.casefold():
                return port
        raise DeviceError(f"Requested serial port {requested} is not present")
    if not ports or ports[0].score < 100:
        summary = ", ".join(f"{p.device}:{p.description}" for p in ports) or "none"
        raise DeviceError(f"No C12880 controller candidate found; ports: {summary}")
    return ports[0]


class C12880Device:
    def __init__(
        self,
        port: str | None = None,
        exposure_ms: float = DEFAULT_EXPOSURE_MS,
        *,
        output_mask: int = DEFAULT_OUTPUT_MASK,
        trigger_mode: TriggerMode = "internal",
        warmup_attempts: int = 8,
    ) -> None:
        exposure_ticks(exposure_ms)
        self.requested_port = port
        self.exposure_ms = float(exposure_ms)
        self.output_mask = int(output_mask)
        self.trigger_mode: TriggerMode = trigger_mode
        self.warmup_attempts = max(1, int(warmup_attempts))
        self.descriptor: PortDescriptor | None = None
        self.serial: serial.Serial | None = None
        self.sequence = 0
        self.last_raw = b""
        self.identity_raw = b""
        self.correction_raw = b""
        self.correction_words = np.empty(0, dtype=np.uint16)
        self._lock = threading.RLock()

    @property
    def is_open(self) -> bool:
        return bool(self.serial and self.serial.is_open)

    @property
    def identity_valid(self) -> bool:
        return identity_is_valid(self.identity_raw)

    def _probe_identity(self, port: str) -> bytes:
        # The vendor performs one availability open/close before the identity open.
        with serial.Serial(port, IDENTITY_BAUD_RATE, timeout=2.0):
            pass
        time.sleep(0.02)
        with serial.Serial(port, IDENTITY_BAUD_RATE, timeout=2.0) as handle:
            handle.reset_input_buffer()
            handle.write(IDENTITY_REQUEST)
            handle.flush()
            time.sleep(0.2)
            return handle.read(IDENTITY_REPLY_BYTES)

    def _read_device_correction(self) -> None:
        assert self.serial is not None
        self.serial.reset_input_buffer()
        self.serial.write(CORRECTION_REQUEST)
        self.serial.flush()
        time.sleep(1.0)
        # The vendor worker performs 512 individual two-byte reads.  The
        # controller does not reliably satisfy one large 1024-byte read on
        # Windows even when every correction word is already queued.
        words = bytearray()
        for _ in range(CORRECTION_BYTES // 2):
            word = self.serial.read(2)
            if len(word) != 2:
                break
            words.extend(word)
        self.correction_raw = bytes(words)
        complete = len(self.correction_raw) // 2 * 2
        self.correction_words = np.frombuffer(
            self.correction_raw[:complete], dtype="<u2"
        ).copy()
        self.serial.reset_input_buffer()

    def connect(self) -> PortDescriptor:
        with self._lock:
            if self.is_open:
                assert self.descriptor is not None
                return self.descriptor
            if self.requested_port:
                descriptor = choose_port(self.requested_port)
                candidates = [descriptor]
            else:
                candidates = enumerate_ports()
                descriptor = None

            # VID/PID is not authoritative: this controller enumerates as an
            # STM32 VCP, while an unrelated CH343 may also be connected. Match
            # the vendor finder by selecting the port whose eight-byte response
            # contains ASCII "c12880" at bytes 2..7.
            self.identity_raw = b""
            for candidate in candidates:
                try:
                    identity = self._probe_identity(candidate.device)
                except (OSError, serial.SerialException):
                    continue
                if identity_is_valid(identity):
                    descriptor = candidate
                    self.identity_raw = identity
                    break
                if self.requested_port:
                    self.identity_raw = identity

            if descriptor is None:
                summary = ", ".join(
                    f"{port.device}:{port.description}" for port in candidates
                ) or "none"
                raise DeviceError(
                    f"No serial port returned the C12880 identity; ports: {summary}"
                )
            try:
                handle = serial.Serial(
                    descriptor.device,
                    BAUD_RATE,
                    bytesize=8,
                    parity="N",
                    stopbits=1,
                    timeout=0.25,
                    write_timeout=None,
                    xonxoff=False,
                    rtscts=False,
                    dsrdtr=False,
                )
                handle.dtr = True
                handle.rts = True
                try:
                    handle.set_buffer_size(rx_size=8_000, tx_size=20)
                except (AttributeError, OSError, serial.SerialException):
                    pass
                time.sleep(0.5)
            except (OSError, serial.SerialException) as exc:
                raise DeviceError(f"Unable to open {descriptor.device}: {exc}") from exc
            self.descriptor = descriptor
            self.serial = handle
            try:
                # Match the packaged worker: one-second startup delay, FF 09, then
                # one-second correction-memory read before live acquisition.
                time.sleep(1.0)
                self._read_device_correction()
                self.serial.reset_input_buffer()
            except (OSError, serial.SerialException) as exc:
                self.close()
                raise DeviceError(f"C12880 initialization failed: {exc}") from exc
            return descriptor

    def close(self) -> None:
        with self._lock:
            if self.serial:
                try:
                    self.serial.close()
                finally:
                    self.serial = None

    def set_exposure(self, exposure_ms: float) -> None:
        exposure_ticks(exposure_ms)
        with self._lock:
            changed = abs(self.exposure_ms - float(exposure_ms)) > 1e-12
            self.exposure_ms = float(exposure_ms)
            if not self.is_open or not changed:
                return
            assert self.serial is not None
            self.serial.write(
                build_exposure_command(self.exposure_ms, self.output_mask)
            )
            self.serial.flush()
            time.sleep(0.1)
            self.serial.reset_input_buffer()

    def set_output_mask(self, output_mask: int) -> None:
        if not 0 <= int(output_mask) <= 0x03:
            raise ValueError("output mask must be 0-3")
        with self._lock:
            changed = self.output_mask != int(output_mask)
            self.output_mask = int(output_mask)
            if self.is_open and changed:
                assert self.serial is not None
                self.serial.write(
                    build_exposure_command(self.exposure_ms, self.output_mask)
                )
                self.serial.flush()
                time.sleep(0.1)
                self.serial.reset_input_buffer()

    def set_trigger_mode(self, trigger_mode: TriggerMode) -> None:
        if trigger_mode not in ("internal", "external"):
            raise ValueError("trigger mode must be internal or external")
        self.trigger_mode = trigger_mode

    def _read_frame_bytes(self, timeout_s: float) -> bytes:
        assert self.serial is not None
        previous_timeout = self.serial.timeout
        if previous_timeout != timeout_s:
            self.serial.timeout = timeout_s
        try:
            # Let the OS serial driver block until the complete packet arrives.
            # This avoids Windows rounding sub-millisecond polling sleeps up to a
            # scheduler tick on every partial packet.
            return self.serial.read(FRAME_BYTES)
        finally:
            if self.serial.timeout != previous_timeout:
                self.serial.timeout = previous_timeout

    def acquire(self, exposure_ms: float | None = None) -> SpectrumFrame:
        with self._lock:
            if not self.is_open:
                self.connect()
            assert self.serial is not None
            if exposure_ms is not None:
                self.set_exposure(float(exposure_ms))
            exposure = self.exposure_ms
            timeout_s = max(0.25, exposure / 1_000.0 + 0.20)
            last_error: Exception | None = None
            for _ in range(self.warmup_attempts):
                if self.trigger_mode == "internal":
                    self.serial.reset_input_buffer()
                    self.serial.write(build_read_command(exposure, self.output_mask))
                    self.serial.flush()
                raw = self._read_frame_bytes(timeout_s)
                self.last_raw = raw
                try:
                    frame = decode_frame(
                        raw,
                        sequence=self.sequence,
                        exposure_ms=exposure,
                        source=f"hardware:{self.serial.port}",
                    )
                except (ShortFrameError, ZeroFrameError) as exc:
                    last_error = exc
                    time.sleep(0.0025)
                    continue
                self.sequence += 1
                return frame
            assert last_error is not None
            raise last_error

    def diagnostic_report(self) -> dict[str, object]:
        return {
            "identity_raw_hex": self.identity_raw.hex(" "),
            "identity_valid": self.identity_valid,
            "correction_bytes": len(self.correction_raw),
            "correction_nonzero": sum(value != 0 for value in self.correction_raw),
            "output_mask": self.output_mask,
            "trigger_mode": self.trigger_mode,
            "exposure_ms": self.exposure_ms,
        }


def port_report(ports: Iterable[PortDescriptor] | None = None) -> list[dict[str, object]]:
    return [port.to_dict() for port in (ports if ports is not None else enumerate_ports())]
