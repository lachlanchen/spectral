# C12880MA Clean-Room Firmware Record

## Scope

`firmware/sdk/` is a new implementation for the STM32H743 controller. It is
derived from documented hardware behavior and interface-level observations.
Private firmware images, Ghidra projects, recovered pseudocode, MCU unique data,
and future EEPROM dumps remain under ignored `firmware/private/` paths.

## Facts established before implementation

- MCU family: STM32H74x/75x, 2 MiB dual-bank internal flash.
- CPU configuration in the existing firmware: 400 MHz.
- Sensor signals: `PE2=ST`, `PE3=CLK`, `PA0=ADC1_INP16`.
- External trigger: `PE9`; mode outputs: `PE13/PE14`.
- USB uses the HS controller with its embedded full-speed PHY and 64-byte data
  packets, not an external ULPI high-speed PHY.
- Legacy acquisition: 12 pre-clocks, integration clocks, 91 dummy clocks,
  294 ADC samples, and a 590-byte response.
- The calibration EEPROM responds at 8-bit I2C address `0xA0` with 16-bit
  memory addressing.

## New implementation choices

The performance path uses TIM2-triggered DMA writes to GPIOE BSRR because PE3
has no suitable timer-output alternate function. ADC1 uses TIM2 TRGO and DMA.
Two non-cacheable frame slots decouple capture and USB transmission. A V2
protocol adds framing, timestamps, status, sequence numbers, drops, and CRC.
Legacy reads are retained; EEPROM writes are intentionally absent.

## Recovery statement

The stored 2 MiB image is sufficient to restore internal application flash if
SWD remains functional. It is not yet a complete board snapshot because option
bytes, OTP state, and external EEPROM calibration are separate. No new firmware
may be flashed until those remaining read-only backups are complete and the
user explicitly authorizes programming.

