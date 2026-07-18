"""Wavelength, pixel-response, dark-reference, and summary calculations."""

from __future__ import annotations

from dataclasses import dataclass
import json
from importlib import resources
from pathlib import Path

import numpy as np

from .protocol import ADC_MAX, PIXEL_COUNT


@dataclass(frozen=True, slots=True)
class WavelengthCalibration:
    mode: str
    coefficients: tuple[float, ...]
    pixel_origin: float = 0.0
    label: str = "nominal"

    def wavelengths(self) -> np.ndarray:
        pixels = np.arange(PIXEL_COUNT, dtype=np.float64) + self.pixel_origin
        if self.mode == "polynomial":
            result = np.zeros_like(pixels)
            for order, coefficient in enumerate(self.coefficients):
                result += coefficient * np.power(pixels, order)
            return result
        if self.mode == "linear":
            start, stop = self.coefficients
            return np.linspace(start, stop, PIXEL_COUNT, dtype=np.float64)
        raise ValueError(f"Unsupported wavelength calibration mode: {self.mode}")

    @classmethod
    def bundled(cls) -> "WavelengthCalibration":
        resource = resources.files("spectral").joinpath("resources/calibration.json")
        payload = json.loads(resource.read_text(encoding="utf-8"))
        return cls(
            mode=payload["mode"],
            coefficients=tuple(float(v) for v in payload["coefficients"]),
            pixel_origin=float(payload.get("pixel_origin", 0.0)),
            label=str(payload.get("label", "nominal")),
        )

    @classmethod
    def from_file(cls, path: str | Path) -> "WavelengthCalibration":
        payload = json.loads(Path(path).read_text(encoding="utf-8"))
        return cls(
            mode=payload["mode"],
            coefficients=tuple(float(v) for v in payload["coefficients"]),
            pixel_origin=float(payload.get("pixel_origin", 0.0)),
            label=str(payload.get("label", Path(path).stem)),
        )


@dataclass(frozen=True, slots=True)
class SpectrumMetrics:
    peak_nm: float
    peak_counts: float
    integrated_counts_nm: float
    mean_counts: float
    saturated_pixels: int
    dynamic_range: float


def process_counts(
    counts: np.ndarray,
    *,
    dark: np.ndarray | None = None,
    correction: np.ndarray | None = None,
) -> np.ndarray:
    result = np.asarray(counts, dtype=np.float64).copy()
    if dark is not None:
        result -= np.asarray(dark, dtype=np.float64)
    np.maximum(result, 0.0, out=result)
    if correction is not None:
        result *= np.asarray(correction, dtype=np.float64)
    return result


def metrics(wavelengths: np.ndarray, counts: np.ndarray) -> SpectrumMetrics:
    peak_index = int(np.argmax(counts))
    low = float(np.percentile(counts, 5))
    high = float(np.percentile(counts, 95))
    return SpectrumMetrics(
        peak_nm=float(wavelengths[peak_index]),
        peak_counts=float(counts[peak_index]),
        integrated_counts_nm=float(np.trapezoid(counts, wavelengths)),
        mean_counts=float(np.mean(counts)),
        saturated_pixels=int(np.count_nonzero(counts >= ADC_MAX)),
        dynamic_range=max(0.0, high - low),
    )

