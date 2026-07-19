# Firmware Variants and Release Tags

The project now preserves four distinct recovery and development artifacts.
They are intentionally not interchangeable.

| Artifact | Version | Purpose | Public |
|---|---:|---|---|
| Reconstruction | 0.1.0 | Conservative clean-room reproduction of the observed vendor protocol and blocking acquisition sequence | Source and compiled hashes |
| Performance | 0.3.0 | TIM2/GPIO DMA, ADC DMA, two frame slots, legacy compatibility, and V2 streaming | Source and compiled hashes |
| Coordinator | 0.1.0 | Independent dual-lamp PWM, telemetry, cooling, LUT, and trigger controller | Source and compiled hashes |
| Original backup | vendor snapshot | Exact 2 MiB read-back used only for recovery | SHA-256 only; binary stays ignored/private |

“Reconstruction” is exact at the documented external contract: identity,
byte order, correction read, integration timing basis, acquisition sequence,
and 590-byte response layout. It is not claimed to be source- or
binary-identical to proprietary firmware. Both new controller images remain
compile-only until hardware validation and explicit flash authorization.

Build all three source images and regenerate `BUILD-MANIFEST.json`:

```powershell
./scripts/build_c12880_firmware_suite.ps1
```

The release commit carries separate annotated tags for the desktop GUI, web
app, reconstruction firmware, performance firmware, coordinator firmware, and
the aggregate suite. `RELEASE-MANIFEST.json` is the authoritative mapping.
