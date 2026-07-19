#!/usr/bin/env python3
"""Build a flat-intensity, high-spectral-span dual-lamp LUT from calibration CSVs."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import numpy as np


def load(path: Path) -> tuple[np.ndarray, np.ndarray, np.ndarray, list[str]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        rows = list(csv.DictReader(handle))
    if not rows or "duty" not in rows[0] or "intensity" not in rows[0]:
        raise ValueError(f"{path}: expected duty,intensity,ch0.. columns")
    channels = sorted(
        (name for name in rows[0] if name.startswith("ch") and name[2:].isdigit()),
        key=lambda name: int(name[2:]),
    )
    if len(channels) < 2:
        raise ValueError(f"{path}: at least two spectral channels are required")
    duty = np.asarray([float(row["duty"]) for row in rows])
    intensity = np.asarray([float(row["intensity"]) for row in rows])
    spectrum = np.asarray([[float(row[name]) for name in channels] for row in rows])
    order = np.argsort(duty)
    return duty[order], intensity[order], spectrum[order], channels


def write_header(path: Path, a: np.ndarray, b: np.ndarray) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    def values(data: np.ndarray) -> str:
        return ", ".join(str(int(np.clip(round(v), 0, 65535))) for v in data)
    path.write_text(
        "#ifndef AGINTI_GENERATED_DUAL_LAMP_LUT_H\n"
        "#define AGINTI_GENERATED_DUAL_LAMP_LUT_H\n"
        f"#define AGINTI_GENERATED_LUT_COUNT {len(a)}U\n"
        f"static const unsigned short aginti_lut1[{len(a)}] = {{{values(a)}}};\n"
        f"static const unsigned short aginti_lut2[{len(b)}] = {{{values(b)}}};\n"
        "#endif\n",
        encoding="ascii",
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--lamp1", type=Path, required=True)
    parser.add_argument("--lamp2", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--header", type=Path)
    parser.add_argument("--steps", type=int, default=64)
    parser.add_argument("--target", type=float)
    parser.add_argument("--tolerance", type=float, default=0.03)
    parser.add_argument("--intensity-weight", type=float, default=20.0)
    parser.add_argument("--spectral-weight", type=float, default=1.0)
    parser.add_argument("--smoothness-weight", type=float, default=0.2)
    args = parser.parse_args()

    d1, i1, s1, names1 = load(args.lamp1)
    d2, i2, s2, names2 = load(args.lamp2)
    if names1 != names2:
        raise ValueError("lamp calibration files use different spectral columns")
    pair_i = (i1[:, None] + i2[None, :]).reshape(-1)
    pair_d1 = np.repeat(d1, len(d2))
    pair_d2 = np.tile(d2, len(d1))
    pair_s = (s1[:, None, :] + s2[None, :, :]).reshape(-1, s1.shape[1])
    target = float(args.target) if args.target is not None else float(np.quantile(pair_i, 0.55))
    relative = np.abs(pair_i - target) / max(abs(target), 1e-12)
    candidates = np.flatnonzero(relative <= args.tolerance)
    if len(candidates) < max(16, args.steps):
        candidates = np.argsort(relative)[: max(256, args.steps * 8)]
    if len(candidates) > 8192:
        candidates = candidates[np.linspace(0, len(candidates) - 1, 8192).astype(int)]

    spectra = pair_s[candidates]
    shape = spectra / np.maximum(spectra.sum(axis=1, keepdims=True), 1e-12)
    centered = shape - shape.mean(axis=0, keepdims=True)
    _, _, vt = np.linalg.svd(centered, full_matrices=False)
    projection = centered @ vt[0]
    endpoint0 = int(np.argmin(projection))
    endpoint1 = int(np.argmax(projection))
    forward_steps = max(2, args.steps)
    chosen: list[int] = []
    previous = np.array([pair_d1[candidates[endpoint0]], pair_d2[candidates[endpoint0]]])
    duty_span = np.maximum([np.ptp(d1), np.ptp(d2)], 1.0)
    for phase in np.linspace(0.0, 1.0, forward_steps):
        target_shape = (1.0 - phase) * shape[endpoint0] + phase * shape[endpoint1]
        spectral_cost = np.mean((shape - target_shape) ** 2, axis=1)
        intensity_cost = ((pair_i[candidates] - target) / max(abs(target), 1e-12)) ** 2
        duties = np.column_stack((pair_d1[candidates], pair_d2[candidates]))
        smoothness = np.sum(((duties - previous) / duty_span) ** 2, axis=1)
        cost = (args.intensity_weight * intensity_cost +
                args.spectral_weight * spectral_cost +
                args.smoothness_weight * smoothness)
        selected = int(np.argmin(cost))
        chosen.append(int(candidates[selected]))
        previous = duties[selected]
    closed = chosen + chosen[-2:0:-1]
    out_d1 = pair_d1[closed]
    out_d2 = pair_d2[closed]
    out_i = pair_i[closed]
    out_s = pair_s[closed]
    q16_scale = 65535.0 / max(float(max(np.max(d1), np.max(d2))), 1.0)
    q1 = np.clip(np.rint(out_d1 * q16_scale), 0, 65535)
    q2 = np.clip(np.rint(out_d2 * q16_scale), 0, 65535)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as handle:
        fields = ["phase", "duty1_q16", "duty2_q16", "predicted_intensity",
                  "relative_intensity_error", *names1]
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for index in range(len(closed)):
            row = {
                "phase": index / max(len(closed) - 1, 1),
                "duty1_q16": int(q1[index]),
                "duty2_q16": int(q2[index]),
                "predicted_intensity": out_i[index],
                "relative_intensity_error": (out_i[index] - target) / max(abs(target), 1e-12),
            }
            row.update({name: out_s[index, j] for j, name in enumerate(names1)})
            writer.writerow(row)
    if args.header:
        write_header(args.header, q1, q2)
    print(f"target={target:.9g}")
    print(f"lut_points={len(closed)}")
    print(f"intensity_rms_relative={np.sqrt(np.mean(((out_i-target)/target)**2)):.6g}")
    print(f"intensity_peak_relative={np.max(np.abs((out_i-target)/target)):.6g}")
    print(f"spectral_endpoint_distance={np.linalg.norm(shape[endpoint1]-shape[endpoint0]):.6g}")


if __name__ == "__main__":
    main()
