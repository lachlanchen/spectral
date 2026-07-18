# Wavelength Rendering and Frame-Rate Limits

## Wavelength-respecting fill

The Qt and web plots use one continuous horizontal gradient; it is not a
repeating texture. Color stops are now expressed in nanometers and converted to
plot coordinates:

```text
340 UV -> 380 violet -> 440 blue -> 490 cyan -> 510 green
       -> 580 yellow -> 645 red -> 700 deep red -> 850 NIR
```

UV and NIR are outside human vision, so their muted violet and deep-red colors
are visual conventions rather than literal perceived colors. The measured ADC
counts remain unchanged. The fill is only a coordinate legend under the curve.

## Current measured path

The controller returns 590 bytes per validated frame:

- 12-byte header
- 288 unsigned 16-bit pixels, or 576 bytes
- 2-byte trailer

At 400 fps this is 236,000 response bytes/s, excluding commands and USB
overhead. At 1000 fps it would be 590,000 response bytes/s, or 4.72 Mbit/s of
payload.

The application configures 1,500,000 baud, but the detected controller is an
STM32 USB virtual COM device. The observed 400 fps exceeds the 254 fps ceiling
of a literal 1.5 Mbaud 8-N-1 UART carrying 590 bytes per frame. Therefore the
baud value is probably USB CDC line-coding metadata rather than the physical
wire rate.

## Sensor and controller ceilings

Hamamatsu specifies a C12880MA clock range up to 5 MHz and a shortest complete
scan period of:

```text
381 / 5 MHz = 76.2 us, or about 13,123 scans/s
```

The bare sensor therefore does not prevent 1000 fps. The present bottleneck is
more likely the synchronous host transaction:

```text
reset input -> send one request -> flush -> wait for 590 bytes -> decode
```

Each frame incurs Windows scheduling, USB transaction, firmware turnaround,
Python call, allocation, and locking overhead. A stable 400-500 fps is a
reasonable expectation for this protocol. Reaching 1000 fps reliably likely
requires controller firmware that streams or batches frames continuously,
larger host reads, device-side timestamps, a preallocated ring buffer, and
rendering decoupled at 20-60 fps.

The payload fits within USB Full-Speed bandwidth in principle, so a streaming
firmware target around 1000 fps is technically plausible. It is not guaranteed
by the current vendor firmware and should be verified with raw recording
disabled, minimum valid integration, and sequence/timing checks.

Primary timing source:
[Hamamatsu C12880MA datasheet](https://www.hamamatsu.com/content/dam/hamamatsu-photonics/sites/documents/99_SALES_LIBRARY/ssd/c12880ma_c16767ma_kacc1226e.pdf).
