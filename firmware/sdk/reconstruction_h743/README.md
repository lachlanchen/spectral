# Vendor-Compatible Reconstruction

This target is an independent clean-room rewrite of the externally observed
C12880 controller behavior. It is intended as the conservative recovery image:

- `04 0D 0A` identity and the eight-byte `c12880` reply;
- raw `FF 09` correction-memory read returning 1,024 bytes;
- big-endian integration ticks in terminated `FF AA` and `FF FF` commands;
- 5 MHz timing basis and 590-byte legacy spectrum responses;
- read-only calibration storage and no V2 protocol surface.

It uses the direct GPIO/ADC path to stay close to the observed blocking vendor
sequence. “Compatible reconstruction” means independently reproduced external
behavior; it does not mean byte-identical source or binary. The target is
compile-validated and must not be flashed without an explicit authorization and
the recovery procedure in `docs/c12880-complete-backup-and-recovery.md`.

Build all source variants without flashing:

```powershell
./scripts/build_c12880_firmware_suite.ps1
```
