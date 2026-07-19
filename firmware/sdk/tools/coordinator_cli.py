#!/usr/bin/env python3
"""Host CLI for the AgInTi dual-H7 coordinator line protocol."""

from __future__ import annotations

import argparse
import csv
import time
from pathlib import Path

import serial


def transact(port: serial.Serial, command: str, wait: float = 0.15) -> list[str]:
    port.reset_input_buffer()
    port.write((command.strip() + "\n").encode("ascii"))
    port.flush()
    deadline = time.monotonic() + wait
    lines: list[str] = []
    while time.monotonic() < deadline:
        raw = port.readline()
        if raw:
            lines.append(raw.decode("ascii", errors="replace").strip())
    return lines


def duty_q16(value: str) -> int:
    number = float(value)
    if 0.0 <= number <= 1.0:
        return round(number * 65535.0)
    if 0.0 <= number <= 255.0:
        return round(number * 65535.0 / 255.0)
    if 0.0 <= number <= 65535.0:
        return round(number)
    raise ValueError(f"duty outside 0..1, 0..255, or 0..65535: {value}")


def load_lut(path: Path) -> list[tuple[int, int]]:
    with path.open(newline="", encoding="utf-8-sig") as handle:
        rows = list(csv.DictReader(handle))
    if not 2 <= len(rows) <= 256:
        raise ValueError("LUT must contain 2..256 rows")
    result: list[tuple[int, int]] = []
    for row in rows:
        left = row.get("duty1_q16") or row.get("code_a") or row.get("duty1")
        right = row.get("duty2_q16") or row.get("code_b") or row.get("duty2")
        if left is None or right is None:
            raise ValueError("LUT needs duty1_q16/duty2_q16 or code_a/code_b")
        result.append((duty_q16(left), duty_q16(right)))
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    sub = parser.add_subparsers(dest="action", required=True)
    sub.add_parser("status")
    sub.add_parser("arm")
    sub.add_parser("stop")
    sub.add_parser("clear")
    run = sub.add_parser("run")
    run.add_argument("--cycles", type=int, default=1)
    run.add_argument("--period-ms", type=int, default=3000)
    manual = sub.add_parser("manual")
    manual.add_argument("duty1")
    manual.add_argument("duty2")
    manual.add_argument("--timeout-ms", type=int, default=3000)
    upload = sub.add_parser("upload-lut")
    upload.add_argument("csv", type=Path)
    monitor = sub.add_parser("monitor")
    monitor.add_argument("--seconds", type=float, default=5.0)
    args = parser.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.05) as port:
        if args.action == "status": commands = ["STATUS"]
        elif args.action == "arm": commands = ["ARM"]
        elif args.action == "stop": commands = ["STOP"]
        elif args.action == "clear": commands = ["CLEAR"]
        elif args.action == "run": commands = ["ARM", f"RUN {args.cycles} {args.period_ms}"]
        elif args.action == "manual":
            commands = ["ARM", f"MANUAL {duty_q16(args.duty1)} {duty_q16(args.duty2)} {args.timeout_ms}"]
        elif args.action == "upload-lut":
            lut = load_lut(args.csv)
            commands = ["STOP", f"LUTLEN {len(lut)}"]
            commands.extend(f"LUT {i} {a} {b}" for i, (a, b) in enumerate(lut))
        else:
            port.write(b"TELEM 1\n")
            deadline = time.monotonic() + args.seconds
            while time.monotonic() < deadline:
                line = port.readline().decode("ascii", errors="replace").strip()
                if line:
                    print(line)
            return
        for command in commands:
            for response in transact(port, command):
                print(response)


if __name__ == "__main__":
    main()
