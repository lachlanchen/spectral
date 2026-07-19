# C12880 Controller Firmware: Read-Only Analysis

## Safety Contract

No firmware has been programmed, erased, unlocked, or modified. The ST-Link
session used only SWD initialization, halt, memory read, `dump_image`, and
`reset run`. Do not use readout-unprotect: changing RDP can mass-erase flash.
No write will be performed until the owner explicitly authorizes it.

The private firmware, strings, and disassembly are stored under
`firmware/private/`, which is excluded from Git. Only reproducible tools,
hashes, and derived findings are published.

## Verified Backup

| Item | Value |
|---|---|
| Target | STM32H74x/75x, Cortex-M7 r1p1 |
| Target voltage | 3.20 V |
| Internal flash | 2 MiB, two 1 MiB banks |
| Flash range | `0x08000000` to `0x081FFFFF` |
| Backup size | 2,097,152 bytes |
| SHA-256 | `67F1F6C421D56C2077D5A3F7417AA6F5213A2791D0C63AE5DAFBDBDF461764B4` |
| Initial MSP | `0x24007F28` |
| Reset vector | `0x08100025` |
| Write operations | 0 |

Bank 1 contains 20,467 programmed bytes through `0x080050A8`. Bank 2
contains 20,090 programmed bytes through `0x08104F28`. The vector table is in
bank 1 while the reset handler is in bank 2, so the linked image intentionally
places code and data across both banks. Neither bank may be treated as spare.

## Static Findings

The stripped image contains USB descriptors `CDC Config`, `CDC Interface`,
`Virtual ComPort`, the identity `c12880`, and `MicroVPC`. No RTOS identity was
found. Non-default peripheral vectors are IRQ 7, 23, 37, and 77, corresponding
on STM32H743-class devices to EXTI1, EXTI9_5, USART1, and USB OTG HS. No ADC or
DMA-stream interrupt vector is active; polling or polled DMA is therefore
likely, although exact analog capture topology still requires board tracing.

The frame path is directly visible in the Thumb disassembly:

| Address | Observation |
|---|---|
| `0x08104BAA` | Compares command byte with `0xAA` |
| `0x08104BFA` | Compares command state with `0xFF` |
| `0x08104C8C` | Loads a 590-byte frame length |
| `0x08104CBA` | Prepares a second 590-byte transfer path |
| `0x08104CC2` | Calls the transfer routine |
| `0x08104CCC` | Busy-waits for transfer completion |

This confirms serialized request, acquisition, response, and completion-wait
behavior. It explains why host-side optimization alone cannot fully exploit
the STM32H7 or sensor timing capability.

## Performance Opportunity

The current measured rate is approximately 420 frames/s. At 590 bytes/frame,
1,000 frames/s requires 590 kB/s payload before USB framing. This is plausible
for USB full-speed bulk transfer but leaves limited margin for CDC overhead and
cannot be reached reliably with a blocking request/response loop.

Recommended firmware architecture:

1. Timer-generated 5 MHz sensor clock and hardware-timed ST/TRG sequencing.
2. ADC or external-converter capture into ping-pong DMA buffers.
3. Continuous USB bulk/CDC transmission from the completed buffer while the
   next frame is acquired.
4. Sequence number, hardware timestamp, exposure, status flags, and CRC in
   every frame.
5. A start/stop streaming command while retaining legacy `0xAA` compatibility.
6. Cache-line-aligned DMA buffers in a DMA-accessible SRAM region with explicit
   STM32H7 cache maintenance.

The C12880MA timing specification permits a minimum scan cycle of 76.2 us at a
5 MHz clock, so the sensor scan timing is not the primary barrier to a 1 kHz
target. The first safe optimization should still be host-only: remove
per-frame input-buffer resets, implement persistent packet framing, and
benchmark. A replacement firmware should be developed and tested on a second
controller or recoverable bench setup before any write to this board.

## Reproduction

Create another private read-only backup:

```powershell
.\scripts\backup_c12880_firmware.ps1
```

Analyze an existing dump offline:

```powershell
.\.venv\Scripts\python.exe .\scripts\analyze_c12880_firmware.py `
  .\firmware\private\<backup>\stm32h74x75x_internal_flash_0x08000000_2MiB.bin
```

The analyzer generates bank extracts, JSON metadata, strings, and ARM Thumb
disassembly only beside the private input image.
