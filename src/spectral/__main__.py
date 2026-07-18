"""Command-line entry point for probing, plotting, and the desktop GUI."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

from .device import C12880Device, enumerate_ports, port_report
from .protocol import DEFAULT_EXPOSURE_MS


def probe(port: str | None, exposure_ms: float) -> int:
    report: dict[str, object] = {"ports": port_report(enumerate_ports())}
    device = C12880Device(port, exposure_ms)
    try:
        descriptor = device.connect()
        report["selected"] = descriptor.to_dict()
        report["controller"] = device.diagnostic_report()
        frame = device.acquire()
        report["frame"] = {
            "valid": True,
            "pixels": int(frame.counts.size),
            "minimum": float(frame.counts.min()),
            "maximum": float(frame.counts.max()),
            "prefix_words": frame.prefix_words,
            "raw_bytes": frame.raw_size,
            "exposure_ms": frame.exposure_ms,
        }
        result = 0
    except Exception as exc:
        report["frame"] = {
            "valid": False,
            "error": f"{type(exc).__name__}: {exc}",
            "raw_bytes": len(device.last_raw),
            "raw_nonzero": sum(bool(value) for value in device.last_raw),
            "raw_head_hex": device.last_raw[:64].hex(" "),
        }
        result = 2
    finally:
        device.close()
    print(json.dumps(report, ensure_ascii=False, indent=2))
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="C12880MA Spectrum Studio")
    parser.add_argument("--port", help="Explicit serial port, for example COM5")
    parser.add_argument(
        "--exposure-ms", type=float, default=DEFAULT_EXPOSURE_MS,
        help=f"Integration time in milliseconds (default: {DEFAULT_EXPOSURE_MS:g})",
    )
    parser.add_argument("--probe", action="store_true", help="Probe once and print JSON")
    parser.add_argument("--demo", action="store_true", help="Show only the vendor reference")
    parser.add_argument("--plot-example", metavar="PNG", help="Export the vendor reference plot")
    args = parser.parse_args(argv)
    if args.probe:
        return probe(args.port, args.exposure_ms)
    if args.plot_example:
        from .plotting import export_vendor_sample
        path = export_vendor_sample(Path(args.plot_example))
        print(path.resolve())
        return 0
    from .gui import run_gui
    return run_gui(requested_port=args.port, demo_only=args.demo)


if __name__ == "__main__":
    sys.exit(main())
