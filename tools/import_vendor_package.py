"""Import the vendor workbooks without executing the unsigned vendor program."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path
import shutil

import xlrd


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("--project", type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    source = args.source.resolve()
    project = args.project.resolve()
    raw = project / "references" / "vendor" / "raw" / "c12880-controller"
    raw.mkdir(parents=True, exist_ok=True)
    for item in source.iterdir():
        target = raw / item.name
        if item.is_file():
            shutil.copy2(item, target)

    sample_book = xlrd.open_workbook(source / "光谱数据.xls")
    sample_sheet = sample_book.sheet_by_index(0)
    values = [float(sample_sheet.cell_value(row, 0)) for row in range(288)]
    resource_dir = project / "src" / "spectral" / "resources"
    resource_dir.mkdir(parents=True, exist_ok=True)
    with (resource_dir / "vendor_sample.csv").open("w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["pixel", "wavelength_nm", "counts"])
        for pixel, value in enumerate(values):
            wavelength = 340.0 + pixel * (850.0 - 340.0) / 287.0
            writer.writerow([pixel, f"{wavelength:.9f}", f"{value:.9f}"])

    correction_book = xlrd.open_workbook(source / "像素校准.xls")
    correction_sheet = correction_book.sheet_by_index(0)
    correction = [float(correction_sheet.cell_value(row, 0)) for row in range(288)]
    (resource_dir / "pixel_correction.json").write_text(
        json.dumps({"source": "vendor workbook column A", "values": correction}, indent=2),
        encoding="utf-8",
    )
    print(f"Imported {len(values)} sample pixels and {len(correction)} correction factors")
    print(f"Raw local copy: {raw}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

