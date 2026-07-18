"""Thread-safe full-rate CSV recording."""

from __future__ import annotations

import csv
from pathlib import Path
import threading

import numpy as np

from .protocol import SpectrumFrame


class CsvRecorder:
    def __init__(self, wavelengths: np.ndarray):
        self.wavelengths = np.asarray(wavelengths, dtype=np.float64)
        self.path: Path | None = None
        self._handle = None
        self._writer = None
        self._lock = threading.RLock()
        self.frames_written = 0

    @property
    def active(self) -> bool:
        return self._writer is not None

    def start(self, path: str | Path) -> Path:
        with self._lock:
            self.stop()
            destination = Path(path)
            destination.parent.mkdir(parents=True, exist_ok=True)
            self._handle = destination.open("w", newline="", encoding="utf-8")
            self._writer = csv.writer(self._handle)
            header = ["timestamp_ns", "sequence", "exposure_ms", "source"]
            header.extend(f"{value:.6f}nm" for value in self.wavelengths)
            self._writer.writerow(header)
            self.path = destination
            self.frames_written = 0
            return destination

    def write(self, frame: SpectrumFrame) -> None:
        with self._lock:
            if self._writer is None:
                return
            row = [frame.timestamp_ns, frame.sequence, frame.exposure_ms, frame.source]
            row.extend(float(value) for value in frame.counts)
            self._writer.writerow(row)
            self.frames_written += 1
            if self.frames_written % 32 == 0:
                self._handle.flush()

    def stop(self) -> Path | None:
        with self._lock:
            previous = self.path
            if self._handle is not None:
                self._handle.flush()
                self._handle.close()
            self._handle = None
            self._writer = None
            return previous

