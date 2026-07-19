"""Read-only backup of EEPROM windows exposed by the vendor C12880 firmware."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

import serial
from serial.tools import list_ports


IDENTITY_BAUD = 256_000
DATA_BAUD = 1_500_000
IDENTITY_REQUEST = b"\x04\r\n"
IDENTITY_BYTES = 8
IDENTITY_PAYLOAD = b"c12880"
READ_ONLY_REQUESTS = {
    "ff09_correction": b"\xff\x09",
    "ff10_calibration": b"\xff\x10",
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def probe_identity(port: str) -> bytes:
    with serial.Serial(port, IDENTITY_BAUD, timeout=2.0):
        pass
    time.sleep(0.02)
    with serial.Serial(port, IDENTITY_BAUD, timeout=2.0) as handle:
        handle.reset_input_buffer()
        handle.write(IDENTITY_REQUEST)
        handle.flush()
        time.sleep(0.2)
        return handle.read(IDENTITY_BYTES)


def choose_port(requested: str | None) -> tuple[str, bytes]:
    candidates = [requested] if requested else [item.device for item in list_ports.comports()]
    for port in candidates:
        if not port:
            continue
        try:
            identity = probe_identity(port)
        except (OSError, serial.SerialException):
            continue
        if len(identity) >= IDENTITY_BYTES and identity[2:8] == IDENTITY_PAYLOAD:
            return port, identity
    visible = ", ".join(item.device for item in list_ports.comports()) or "none"
    raise RuntimeError(f"No C12880 identity response; visible serial ports: {visible}")


def drain_reply(
    handle: serial.Serial,
    request: bytes,
    *,
    maximum_bytes: int,
    idle_seconds: float,
    total_seconds: float,
) -> bytes:
    if request not in READ_ONLY_REQUESTS.values():
        raise RuntimeError("Refusing a command outside the fixed read-only allowlist")
    handle.reset_input_buffer()
    handle.write(request)
    handle.flush()
    started = time.monotonic()
    last_data = started
    result = bytearray()
    while time.monotonic() - started < total_seconds and len(result) < maximum_bytes:
        waiting = handle.in_waiting
        chunk = handle.read(min(max(waiting, 1), maximum_bytes - len(result)))
        if chunk:
            result.extend(chunk)
            last_data = time.monotonic()
            continue
        if result and time.monotonic() - last_data >= idle_seconds:
            break
    return bytes(result)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Repeated, read-only extraction of vendor EEPROM windows."
    )
    parser.add_argument("--port", help="Explicit C12880 serial port, for example COM5")
    parser.add_argument(
        "--output-root",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "firmware" / "private",
    )
    parser.add_argument("--repeats", type=int, default=3, choices=range(2, 6))
    parser.add_argument("--maximum-bytes", type=int, default=8192)
    parser.add_argument("--idle-ms", type=float, default=300.0)
    parser.add_argument("--total-seconds", type=float, default=8.0)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    port, identity = choose_port(args.port)
    stamp = time.strftime("%Y%m%d_%H%M%S")
    output = args.output_root.resolve() / f"c12880_legacy_eeprom_{stamp}"
    output.mkdir(parents=True, exist_ok=False)

    captures: dict[str, list[dict[str, object]]] = {}
    with serial.Serial(
        port,
        DATA_BAUD,
        bytesize=8,
        parity="N",
        stopbits=1,
        timeout=0.05,
        write_timeout=2.0,
        xonxoff=False,
        rtscts=False,
        dsrdtr=False,
    ) as handle:
        handle.dtr = True
        handle.rts = True
        time.sleep(1.0)
        for label, request in READ_ONLY_REQUESTS.items():
            records: list[dict[str, object]] = []
            for index in range(1, args.repeats + 1):
                payload = drain_reply(
                    handle,
                    request,
                    maximum_bytes=args.maximum_bytes,
                    idle_seconds=args.idle_ms / 1000.0,
                    total_seconds=args.total_seconds,
                )
                path = output / f"{label}_read{index}.bin"
                path.write_bytes(payload)
                records.append(
                    {
                        "file": path.name,
                        "bytes": len(payload),
                        "sha256": sha256(payload),
                    }
                )
                time.sleep(0.1)
            captures[label] = records

    consensus: dict[str, dict[str, object]] = {}
    for label, records in captures.items():
        groups: dict[str, int] = {}
        for record in records:
            digest = str(record["sha256"])
            groups[digest] = groups.get(digest, 0) + 1
        digest, count = max(groups.items(), key=lambda item: item[1])
        matching = [record for record in records if record["sha256"] == digest]
        consensus[label] = {
            "sha256": digest,
            "count": count,
            "required": len(records) // 2 + 1,
            "valid": count >= len(records) // 2 + 1,
            "bytes": matching[0]["bytes"],
            "canonical_file": matching[0]["file"],
        }

    manifest = {
        "schema": "aginti.c12880.legacy-eeprom.v1",
        "created_at": time.strftime("%Y-%m-%dT%H:%M:%S%z"),
        "port": port,
        "identity_hex_private": identity.hex(" "),
        "baud": DATA_BAUD,
        "commands": {
            label: request.hex(" ") for label, request in READ_ONLY_REQUESTS.items()
        },
        "captures": captures,
        "consensus": consensus,
        "coverage": {
            "ff09_expected_vendor_bytes": 1024,
            "ff10_expected_vendor_bytes": 48,
            "entire_physical_eeprom_proven": False,
            "statement": (
                "This captures every window exposed by the known legacy read commands. "
                "It does not prove the capacity or coverage of the physical EEPROM."
            ),
        },
        "writes_performed": False,
    }
    (output / "manifest.json").write_text(
        json.dumps(manifest, indent=2), encoding="utf-8"
    )

    accepted = all(item["valid"] for item in consensus.values())
    print(
        json.dumps(
            {
                "output": str(output),
                "port": port,
                "consensus": consensus,
                "read_only": True,
                "accepted": accepted,
            },
            indent=2,
        )
    )
    return 0 if accepted else 2


if __name__ == "__main__":
    sys.exit(main())

