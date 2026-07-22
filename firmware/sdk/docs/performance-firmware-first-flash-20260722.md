# Performance Firmware First-Flash Result

## Decision

Performance firmware `0.3.0` is **rejected for hardware use**. It compiled,
programmed, and passed OpenOCD byte verification, but its USB device did not
enumerate. No identity, correction-memory, or spectrum-frame test was possible.
Do not flash the tagged image until the USB initialization defect is corrected
and a new version is validated.

## Safety and recovery evidence

Before programming, two fresh full-flash reads independently matched the known
original SHA-256:

```text
67F1F6C421D56C2077D5A3F7417AA6F5213A2791D0C63AE5DAFBDBDF461764B4
```

The tested performance BIN was the released 55,136-byte artifact with SHA-256
`2481C4558D8C1D4DBE829C3FBCE3D6A688A88C80B56D348CF46FE78F2E54C6BC`.
OpenOCD reported successful programming, verification, and reset. Windows then
created no STM32 USB device or COM port.

The complete original 2 MiB image was restored and verified. A new full
post-restore readback exactly matched the original SHA-256 above. Option bytes
and the external calibration EEPROM were not modified.

## Restored-system acceptance

The original controller returned as `0483:5740` on `COM3`. Spectrum Studio
validated the eight-byte `c12880` identity, all 1,024 correction bytes, a
590-byte frame, and 288 pixels. During 30 API samples:

- every frame was live and fresh;
- sequence advanced by 402;
- acquisition was approximately 102.7--103.8 fps;
- maximum frame age was 65 ms;
- no pixels saturated;
- desktop GUI and HTTP web application both ran, with HTTP status 200.

These optical values demonstrate transport and visualization plausibility, not
absolute radiometric or wavelength calibration.

## Debug boundary

The failure occurs before the host protocol. Inspect USB clock selection, PCD
initialization, endpoint descriptors, interrupt routing, and the HS-controller
embedded-FS-PHY configuration before changing the capture engine. The DMA
acquisition path has not yet been exercised on hardware and must not be blamed
or accepted based on this trial.
