# AgInTi Spectrum Studio

An independent acquisition and visualization application for the Hamamatsu C12880MA and its supplied USB controller. It reproduces the working vendor transport while keeping acquisition independent from GUI refresh, validating every 288-pixel frame, recording raw spectra, and exposing diagnostics for later system integration.

## Verified hardware protocol

The vendor GUI was traced from process startup while displaying a live spectrum. The controller identifies itself by returning `c12880` at 256,000 baud, then uses 1,500,000-baud, 8-N-1 acquisition. A 10 us request is `FF AA 01 00 00 00 32 0D 0A`; each response is exactly 590 bytes: a 12-byte reserved header, 576 bytes for 288 little-endian 16-bit pixels, and a 2-byte trailer. On this workstation the controller is STM32 VCP `0483:5740` on `COM3`; the CH343 on `COM5` is unrelated.

The application also reproduces the vendor startup correction-memory request (`FF 09`), exposure updates, software/external trigger modes, and OUT2/OUT3 mask.

## Run

```powershell
cd C:\Users\Administrator\Projects\spectral
uv sync --extra vendor
uv run spectrum-studio
```

Probe without starting the GUI:

```powershell
uv run spectrum-studio --probe --port COM3 --exposure-ms 0.01
```

Open only the bundled reference dataset:

```powershell
uv run spectrum-studio --demo
```

## Features

- Active `c12880` identity probing, so selection does not depend on a guessed VID/PID or COM number.
- Vendor-compatible identity, initialization, exposure, output-mask, and trigger transactions.
- Exact 590-byte frame validation and 288-pixel decoding.
- Background acquisition with a 30 Hz GUI refresh, so plotting does not determine capture rate.
- Internal/software and external/TTL trigger modes.
- Integration-time control, frame averaging, dark subtraction, CSV recording, and PNG export.
- Nominal 340-850 nm mapping with support for per-device wavelength coefficients.
- Explicit raw-byte, identity, correction-memory, saturation, and frame-rate diagnostics.

## Calibration limitation

Hamamatsu supplies wavelength-conversion coefficients on each sensor's individual test result sheet. Until those coefficients are entered, the app uses a clearly marked nominal linear 340-850 nm axis. This is suitable for connection testing and qualitative visualization, not calibrated peak metrology.

See [references/protocol.md](references/protocol.md) for the recovered protocol and [references/vendor-recovery.md](references/vendor-recovery.md) for the local source-recovery workflow.
