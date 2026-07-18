# C12880MA Controller Protocol

## Evidence

This clean-room description combines the supplied `C12880 V2.1` manual, Python 3.11 bytecode from `wave_main.exe`, and a live Win32 serial trace captured while the vendor GUI displayed a changing spectrum. The related S11639 and TCD1304 projects are used only to corroborate shared architecture; their pixel counts and frames are not reused.

## Discovery and initialization

1. Open candidate ports at 256,000 baud with a two-second timeout, then close them.
2. Reopen a candidate at 256,000 baud, send `04 0D 0A`, wait 0.2 seconds, and read eight bytes.
3. The vendor checks whether reply bytes `2:8` equal ASCII `c12880`. On the measured workstation this is STM32 VCP `0483:5740` on `COM3`; the CH343 on `COM5` is a different device.
4. Close the identity port and reopen the selected port at 1,500,000 baud, 8-N-1.
5. Use DTR and RTS enabled. The traced Windows DCB flags were `0x1011`; read timeout was 2,000 ms.
6. Set receive/transmit buffers to 8,000/20 bytes where supported.
7. After startup delays, send `FF 09`, wait one second, and read 1,024 correction-memory bytes.

## Live acquisition

Fresh vendor sessions repeatedly write nine-byte requests with CR/LF:

```text
FF AA OO TT TT TT TT 0D 0A
```

- `FF AA`: acquire one spectrum.
- `OO`: output/mode byte. Fresh startup uses `01`; a later manual UI trace used `00`. Spectrum Studio defaults to the startup-safe value `01`.
- `TT TT TT TT`: unsigned big-endian integration ticks.

Observed working requests included:

```text
FF AA 01 00 00 00 1F 0D 0A
FF AA 01 00 00 00 32 0D 0A
```

The traced vendor implementation converts integration time using `milliseconds * 5000`, or five ticks per microsecond. Its 31-tick startup request is approximately 6.2 us; Spectrum Studio defaults to 10 us (`00 00 00 32`). Explicit exposure changes use the same payload with command `FF FF` and append `0D 0A`.

## Response

The live Win32 trace showed one 590-byte read per request:

```text
byte 0..11    reserved header
byte 12..587  288 little-endian unsigned 16-bit pixels
byte 588..589 reserved trailer
```

The vendor GUI issued approximately 538 requests in a six-second trace. Spectrum Studio validates all 590 bytes and rejects incomplete or all-zero frames rather than plotting malformed data.

## Trigger and outputs

In software/internal mode, each frame is initiated by the terminated `FF AA` request. In external mode, the module's pulled-up trigger input is activated by pulling it to 0 V; the manual specifies less than 500 Hz and a minimum interval of exposure time plus approximately 8 ms. OUT2/OUT3 are low-current logic outputs and must not drive loads directly.

## Wavelength axis

The C12880MA covers nominally 340-850 nm. Hamamatsu supplies per-sensor wavelength-conversion coefficients on the individual test sheet. The bundled linear axis remains explicitly labeled nominal until those coefficients are installed.
