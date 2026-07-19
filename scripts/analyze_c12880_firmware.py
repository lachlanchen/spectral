"""Analyze an offline STM32H7 flash dump without touching the device."""

from __future__ import annotations

import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import struct
import subprocess


FLASH_BASE = 0x08000000
BANK_BYTES = 0x00100000
FLASH_BYTES = 2 * BANK_BYTES
VECTOR_WORDS = 200


def entropy(data: bytes) -> float:
    counts = collections.Counter(data)
    return -sum(
        (count / len(data)) * math.log2(count / len(data))
        for count in counts.values()
    ) if data else 0.0


def programmed_extent(data: bytes) -> tuple[int, int]:
    non_erased = [index for index, value in enumerate(data) if value != 0xFF]
    return (len(non_erased), non_erased[-1] + 1 if non_erased else 0)


def disassemble(binary: Path, base: int, output: Path) -> bool:
    objdump = shutil.which("arm-none-eabi-objdump")
    if not objdump:
        return False
    with output.open("w", encoding="ascii", errors="replace") as handle:
        subprocess.run(
            [objdump, "-D", "-b", "binary", "-m", "arm", "-M", "force-thumb",
             f"--adjust-vma=0x{base:08X}", str(binary)],
            check=True,
            stdout=handle,
        )
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("firmware", type=Path)
    args = parser.parse_args()
    firmware = args.firmware.resolve()
    data = firmware.read_bytes()
    if len(data) != FLASH_BYTES:
        raise ValueError(f"Expected {FLASH_BYTES} bytes, received {len(data)}")

    vectors = struct.unpack_from(f"<{VECTOR_WORDS}I", data, 0)
    default_handler = vectors[16]
    active_vectors = []
    for index, value in enumerate(vectors):
        address = value & ~1
        if not value or value == default_handler:
            continue
        if index < 16 or FLASH_BASE <= address < FLASH_BASE + FLASH_BYTES:
            active_vectors.append({
                "index": index,
                "irq": index - 16 if index >= 16 else None,
                "address": f"0x{value:08X}",
            })

    banks = []
    for index in range(2):
        base = FLASH_BASE + index * BANK_BYTES
        bank = data[index * BANK_BYTES:(index + 1) * BANK_BYTES]
        programmed, extent = programmed_extent(bank)
        aligned_extent = (extent + 0xFFF) & ~0xFFF
        used_path = firmware.parent / f"bank{index + 1}-used.bin"
        used_path.write_bytes(bank[:aligned_extent])
        disassembly_path = firmware.parent / f"bank{index + 1}-thumb-disassembly.txt"
        disassembled = disassemble(used_path, base, disassembly_path)
        banks.append({
            "bank": index + 1,
            "base": f"0x{base:08X}",
            "sha256": hashlib.sha256(bank).hexdigest().upper(),
            "programmed_bytes": programmed,
            "used_extent_bytes": extent,
            "used_end": f"0x{base + extent:08X}",
            "disassembled": disassembled,
        })

    strings = [
        {"address": f"0x{FLASH_BASE + match.start():08X}",
         "text": match.group().decode("ascii", "replace")}
        for match in re.finditer(rb"[ -~]{4,}", data)
    ]
    (firmware.parent / "firmware-strings.json").write_text(
        json.dumps(strings, indent=2), encoding="utf-8"
    )
    report = {
        "source": str(firmware),
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest().upper(),
        "entropy_bits_per_byte": round(entropy(data), 4),
        "initial_msp": f"0x{vectors[0]:08X}",
        "reset_vector": f"0x{vectors[1]:08X}",
        "default_handler": f"0x{default_handler:08X}",
        "active_vectors": active_vectors,
        "banks": banks,
        "ascii_string_count": len(strings),
    }
    report_path = firmware.parent / "firmware-analysis.json"
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
