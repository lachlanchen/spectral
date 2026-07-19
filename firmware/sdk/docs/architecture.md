# Firmware Architecture and Validation Contract

## Evidence classes

- **Verified:** derived from the backed-up binary, its vector table and strings,
  read-only SWD identification, or the working host protocol.
- **Corroborated:** matches an official STM32 or Hamamatsu datasheet and the
  recovered implementation.
- **Designed:** new clean-room behavior that compiles but is not yet measured on
  the board.
- **Unresolved:** requires an oscilloscope, logic analyzer, reference spectrum,
  or controlled first-flash test.

## Verified legacy sequence

The existing application performs 12 pre-clocks, asserts `ST`, clocks for the
requested integration count, deasserts `ST`, emits 91 dummy clocks, and then
acquires 294 ADC values. It sends 590 bytes: a 12-byte zero header, 288 active
pixel words, and one trailer word. Five acquired flush samples are not sent.

The implementation uses GPIO write calls for every edge, polling for every ADC
sample, and blocking frame output. The new firmware preserves the externally
visible legacy layout but removes those serialization points.

## New pipeline

```text
USB command parser              continuous USB CDC IN
        |                               ^
        v                               |
scheduler -> slot A capture -> ready -> transmit
             slot B transmit <- ready <- next capture
                        |
      TIM2 events -> GPIOE BSRR + ADC1 trigger -> DMA
```

Slot memory is placed in D2 SRAM and made non-cacheable with the MPU. The USB
peripheral is configured without internal DMA; its buffers remain CPU-visible.
The capture path never allocates memory.

## Why PE3 requires a DMA-to-GPIO clock

The STM32H743 alternate-function table does not expose a timer output channel
on PE3. Reusing the existing PCB therefore cannot route a normal timer PWM pin
to the sensor clock. The performance backend instead uses TIM2 update and
compare DMA requests. One stream writes the PE3 set mask to `GPIOE_BSRR`; the
other writes the reset mask half a period later. This gives hardware-timed
edges without rewiring.

TIM2 channel 2 creates an internal trigger after the falling clock edge. ADC1
uses `TIM2_TRGO`, channel 16, 16-bit conversion, and DMA. The DMA implementation
uses 90 standalone dummy clocks and makes the first readout cycle the 91st
clock, reproducing the legacy sample phase without a software-triggered first
sample.

## Timing and throughput

At sensor clock `f_clk`, the nominal capture duration is approximately

```text
T_capture = (12 + N_exposure + 90 + 294) / f_clk
```

For a 1 MHz clock and 1 ms integration (`N_exposure=1000`), this is about
1.396 ms before software overhead. At the shortest physical sensor integration,
USB rather than C12880 timing becomes the likely bottleneck.

A 590-byte stream at 1000 frame/s is 590 kB/s before USB framing. This is above
the practical capacity of the recovered 1.5 Mbaud UART but can fit within a
well-scheduled USB full-speed bulk/CDC path. It must be measured; USB full-speed
and Windows CDC scheduling do not guarantee 1000 frame/s.

## Safety and recovery

The internal-flash backup can restore the application bytes if SWD remains
available. It is not a full board image. External EEPROM, option bytes, OTP,
and board-specific protection state are separate. No first flash should happen
until those are recorded and a restore rehearsal has been written down.

## Required staged validation

1. Reconfirm backup hashes and collect EEPROM/option-byte snapshots.
2. Flash only after explicit authorization.
3. Boot with the C12880MA analog path protected from excessive illumination.
4. Scope PE2 and PE3 before trusting ADC data.
5. Verify clock frequency, duty cycle, ST width, and 91-clock readout phase.
6. Capture a dark frame and a stable broadband reference.
7. Compare all 288 pixels against the vendor firmware at equal integration.
8. Sweep 0.1, 0.5, 1, and 2 MHz while checking ADC overrun and spectral error.
9. Stress continuous USB streaming and inspect sequence gaps and CRC failures.
10. Only then tune clocks, sampling time, and queue depth.

