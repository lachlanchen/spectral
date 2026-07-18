"""Static exports independent of the real-time GUI."""

from __future__ import annotations

import csv
from importlib import resources
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


def load_vendor_sample() -> tuple[np.ndarray, np.ndarray]:
    source = resources.files("spectral").joinpath("resources/vendor_sample.csv")
    wavelengths: list[float] = []
    counts: list[float] = []
    with source.open("r", encoding="utf-8", newline="") as handle:
        for row in csv.DictReader(handle):
            wavelengths.append(float(row["wavelength_nm"]))
            counts.append(float(row["counts"]))
    return np.asarray(wavelengths), np.asarray(counts)


def export_spectrum(
    wavelengths: np.ndarray,
    counts: np.ndarray,
    destination: str | Path,
    *,
    title: str,
    subtitle: str,
) -> Path:
    path = Path(destination)
    path.parent.mkdir(parents=True, exist_ok=True)
    plt.style.use("default")
    fig, ax = plt.subplots(figsize=(13, 6.8), dpi=180)
    fig.patch.set_facecolor("#f4f0e7")
    ax.set_facecolor("#fffdf8")
    bands = [
        (340, 380, "#7567d8"), (380, 450, "#4d55d9"),
        (450, 495, "#1787d1"), (495, 570, "#32a86b"),
        (570, 590, "#d9b625"), (590, 620, "#ed7b2f"),
        (620, 700, "#d94242"), (700, 850, "#8e5151"),
    ]
    for left, right, color in bands:
        ax.axvspan(left, right, color=color, alpha=0.055, linewidth=0)
    ax.plot(wavelengths, counts, color="#007f76", linewidth=2.2)
    ax.fill_between(wavelengths, counts, color="#2aa69a", alpha=0.13)
    peak = int(np.argmax(counts))
    ax.scatter([wavelengths[peak]], [counts[peak]], s=55, color="#ff6542", zorder=4)
    ax.annotate(
        f"peak {wavelengths[peak]:.1f} nm",
        (wavelengths[peak], counts[peak]),
        xytext=(12, 16), textcoords="offset points", color="#9b351f",
        fontsize=9, weight="bold",
    )
    ax.set_xlim(float(wavelengths.min()), float(wavelengths.max()))
    ax.set_ylim(bottom=0)
    ax.set_xlabel("Wavelength (nm)", color="#243840", weight="bold")
    ax.set_ylabel("ADC counts", color="#243840", weight="bold")
    ax.grid(True, color="#d6d0c4", alpha=0.55, linewidth=0.7)
    ax.spines[["top", "right"]].set_visible(False)
    fig.text(0.075, 0.95, title, fontsize=20, weight="bold", color="#102a35")
    fig.text(0.075, 0.915, subtitle, fontsize=9.5, color="#5c6b70")
    fig.tight_layout(rect=(0.04, 0.04, 0.98, 0.89))
    fig.savefig(path, bbox_inches="tight", facecolor=fig.get_facecolor())
    plt.close(fig)
    return path


def export_vendor_sample(destination: str | Path) -> Path:
    wavelengths, counts = load_vendor_sample()
    return export_spectrum(
        wavelengths,
        counts,
        destination,
        title="C12880MA vendor reference spectrum",
        subtitle="Reference workbook data; nominal linear 340-850 nm axis; not a live or wavelength-calibrated capture",
    )

