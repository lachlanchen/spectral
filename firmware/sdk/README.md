# AgInTi C12880MA Firmware SDK

This directory contains a clean-room STM32H743 firmware and host SDK for the
C12880MA spectrometer controller. It is based on observed interfaces and
read-only hardware analysis; it does not contain the vendor binary or recovered
vendor pseudocode.

## Safety status

The project currently has **compile validation only**. It has not been flashed
to the controller. Before any first flash, preserve all of the following:

- the existing 2 MiB internal-flash image;
- external EEPROM calibration bytes, including `0x0000..0x0fff` and the area
  beginning at `0x1068`;
- option-byte and read-protection register values;
- the MCU UID and a hash manifest of every backup.

The firmware deliberately implements EEPROM reads but no EEPROM writes.

## Recovered board contract

| Function | STM32H743 signal |
|---|---|
| C12880MA `ST` | `PE2` |
| C12880MA `CLK` | `PE3` |
| Analog video | `PA0 / ADC1_INP16` |
| External trigger | `PE9 / EXTI9_5` |
| Output/mode bits | `PE13`, `PE14` |
| Calibration EEPROM | software I2C on `PB6/PB7`, address `0xA0` |
| USB device | `USB_OTG_HS` core with embedded full-speed PHY on `PB14/PB15` |

## Design

- Hardware-timed clock edges via TIM2 requests and DMA writes to `GPIOE_BSRR`.
- ADC1 external triggering plus DMA into non-cacheable D2 SRAM.
- Two frame slots so USB transmission can overlap the next acquisition.
- Legacy 590-byte frames for existing software and a framed V2 stream with
  sequence, timestamp, exposure, status, drop count, and CRC-32.
- A slower `DIRECT` backend for timing comparison and fault isolation.
- No dynamic allocation in the application data path.
- Safe outputs and a non-returning panic path.

The default clock is 1 MHz. The software-configurable range is 100 kHz to
2 MHz until bench validation establishes a reliable ADC timing margin. The
C12880MA's 5 MHz limit is a sensor limit, not a guarantee for this complete
16-bit acquisition chain.

## Fetch and build

From the repository root in PowerShell:

```powershell
./scripts/fetch_c12880_firmware_deps.ps1
./scripts/build_c12880_firmware.ps1 -Engine DMA -Configuration Release
```

Build products are written to `firmware/sdk/build-dma/`:

- `aginti_c12880_h743.elf`
- `aginti_c12880_h743.bin`
- `aginti_c12880_h743.hex`
- `aginti_c12880_h743.map`

The scripts contain no OpenOCD, ST-Link, erase, or program command.

For the reference backend:

```powershell
./scripts/build_c12880_firmware.ps1 -Engine DIRECT -Configuration Release
```

## Host SDK

```powershell
python -m pip install -e firmware/sdk/python
aginti-c12880 --port COM7 identity
aginti-c12880 --port COM7 stream --exposure-us 1000 --clock-hz 1000000
```

V2 continuous streaming avoids one host request per frame. The compatibility
mode remains available for the existing 590-byte request-response protocol.

## Dual-H7 synchronized controller

`coordinator_h743/` is a second, independent firmware target for the Geek
STM32H743IIT6 board. It keeps the spectrometer MCU dedicated to acquisition
while the coordinator owns dual-lamp PWM, slow optical/electrical telemetry,
cooling protection, LUT execution, and the acquisition trigger.

```powershell
./scripts/build_dual_h7_coordinator.ps1
python firmware/sdk/tools/coordinator_cli.py --port COM7 status
```

The coordinator is also compile-validated only. The build script never calls
OpenOCD and never programs either board. Wiring, commands, timing limits, and
the calibration workflow are documented in `docs/dual-h7-control.md`.

## Current validation boundary

Compilation establishes source and link consistency. It does not establish
signal polarity, EEPROM timing, analog settling, ADC phase, optical calibration,
USB throughput, or hardware stability. The staged first-flash and oscilloscope
checks are specified in `docs/architecture.md` and the Chinese monograph under
`publications/c12880_firmware_sdk/`.
