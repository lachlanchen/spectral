# C12880 Controller Backup and Recovery

## Scope

This workflow preserves every safely readable restoration input from the C12880 controller before experimental firmware is flashed. It is deliberately read-only: it does not erase flash, unlock protection, change option bytes, inject code, or reset configuration.

The target already identified through the external ST-Link/V2 is an STM32H74x/H75x device with ID 0x450 and 2 MiB of dual-bank flash. ST documents this memory as two 1 MiB banks with eight 128 KiB sectors per bank.

## What the package captures

| State | Capture | Restoration role |
|---|---|---|
| Internal flash | Three independent 2 MiB reads, strict-majority consensus, plus 1 MiB bank splits | Primary firmware recovery |
| Option state | Current and programmed RDP, PCROP, secure-area, WRP, boot, and bank-swap register views | Manual audit before any option restore |
| Device identity | CPUID, DBGMCU ID, 96-bit UID, flash-size signature | Confirms that the same MCU is being handled |
| System memory | Optional 128 KiB mapped factory window | Reference only; never restored |
| Volatile RAM | Optional best-effort snapshots | Diagnostics only; never restored |
| Host assets | Optional byte-for-byte copy and hash inventory | Preserves calibration tables, manuals, and vendor tools |
| External EEPROM | Imported only from a separately acquired image | Required for a complete board-level nonvolatile backup |

STM32H743 does not provide the STM32F7-style 1 KiB user OTP area. The chip instead has factory system memory and nonvolatile option bytes accessed through flash-interface registers. The script therefore does not guess or read an F7 OTP address.

## Safety gates

The script refuses to proceed when another OpenOCD process is active or when multiple ST-Link/V2 probes are present without an explicit serial number. Its generated target command is scanned for programming, erase, unlock, option-write, and readout-protection commands before OpenOCD starts.

The MCU is briefly halted for a coherent image and then resumed without a reset. A second cleanup connection issues resume even if the main read fails. Active USB acquisition may pause during this interval.

The result is accepted only when:

- Device ID is 0x450.
- Flash-size signature is 2048 KiB.
- Every flash read is exactly 2 MiB.
- At least two of three independently read images have identical SHA-256 hashes.
- The vector table is plausible.
- An optional prior known-good hash also matches.

## Protection cases

| Condition | Safe behavior |
|---|---|
| RDP level 0, value 0xAA | Flash should be readable; continue with repeated verification. |
| RDP level 1, any value except 0xAA/0xCC | Do not regress to level 0. That transition erases user flash. Seek an existing application/bootloader export path. |
| RDP level 2, value 0xCC | Debug is permanently disabled. Do not attempt recovery through SWD. |
| PCROP or secure region | Mark protected bytes unavailable; never remove protection to obtain a backup. |
| Unstable power or SWD | Use a lower SWD clock and three reads. Stop unless a strict-majority consensus exists. |
| Multiple probes | Select the intended probe with the HlaSerial parameter. |
| External EEPROM present | SWD cannot automatically image it. Use the vendor firmware's read API or an external I2C programmer while preventing bus contention. |

### Legacy EEPROM windows

When the controller USB serial port is connected, the original firmware exposes two
known read-only commands. FF 09 returns the correction-memory window used by the
vendor application, normally 1,024 bytes. FF 10 returns a separate calibration
window, normally 48 bytes. Capture each three times:

    python scripts/backup_c12880_legacy_eeprom.py --port COM5

The extractor accepts only the two fixed read commands, drains each response,
stores every raw pass, and requires strict-majority SHA-256 consensus. It does
not claim that these windows cover the entire physical EEPROM. If the physical
device capacity must be preserved beyond the firmware-visible regions, identify
its part number and use an external I2C programmer with the STM32 held reset or
otherwise isolated from PB6/PB7.

## Running the backup

Use the existing SWD wiring. No new wiring is required:

- ST-Link 3.3 V sense to target 3.3 V
- ST-Link GND to target GND
- ST-Link SWDIO/TMS to target TMS/SWDIO
- ST-Link SWCLK/TCK to target TCK/SWCLK
- NRST is optional for this hot-attach read and is not used by the script

Example:

    powershell -ExecutionPolicy Bypass -File scripts/backup_c12880_complete.ps1
      -ExpectedFlashSha256 67F1F6C421D56C2077D5A3F7417AA6F5213A2791D0C63AE5DAFBDBDF461764B4
      -CaptureMappedSystemMemory
      -CaptureVolatileRam
      -HostAssetRoots "D:\BaiduNetdiskDownload\c12880..."
      -CopyHostAssets
      -MirrorRoot "D:\Backups\C12880"

Verify either copy independently:

    powershell -ExecutionPolicy Bypass -File scripts/verify_c12880_backup.ps1
      -BackupDirectory firmware/private/c12880_complete_YYYYMMDD_HHMMSS

Private output belongs under firmware/private, which is ignored by Git. The ZIP is not encrypted; move another verified copy to offline or encrypted storage for a real disaster-recovery backup.

## Restore policy

Do not restore automatically. First verify the target ID, target voltage, package hashes, and present option state. Restore the two user-flash banks at their recorded addresses and verify them byte-for-byte. Restore external EEPROM separately only when its device type, size, addressing mode, and image are known. Change option bytes last, one audited field at a time, only if they differ from the saved state.

Never write the saved UID, factory system memory, calibration signatures, or volatile RAM. Never lower RDP level 1 merely to restore an image, and never set RDP level 2 during development.

## Completeness definition

Matching flash reads plus saved option-register views form a complete MCU user-firmware backup. They do not by themselves form a complete board backup. The latter additionally requires a verified image of every external nonvolatile component, especially the firmware-observed I2C EEPROM on PB6/PB7 at 7-bit address 0x50, if that component is populated.

Authoritative references:

- [ST RM0433 reference manual](https://www.st.com/resource/en/reference_manual/rm0433-stm32h743-753-and-stm32h750-value-line-advanced-arm-based-32-bit-mcus-stmicroelectronics.pdf)
- [ST AN4936 migration note](https://www.st.com/resource/en/application_note/an4936-migration-of-microcontroller-applications-from-stm32f7-series-to-stm32h743753-line-stmicroelectronics.pdf)
- [OpenOCD flash command manual](https://openocd.org/doc/html/Flash-Commands.html)
