# Vendor Source Comparison

## Supplied projects

| Material | Sensor family | Optical pixels | Receive threshold | Applicability |
| --- | --- | ---: | ---: | --- |
| `playviewUSB11639save3/comread_data.py` | S11639 | 2048 | 4004 bytes | Common serial behavior only |
| `TCD1304` package | TCD1304 CCD | Model-specific | Model-specific | Not C12880MA |
| `c12880.../wave_main.exe` recovered bytecode | C12880MA package | 288 | 576-byte wait; 588-byte decoded frame | Primary controller evidence |
| `C12880 V2.1` manual | C12880MA | 288 | Documentation is internally inconsistent | Cross-check with executable |

## Shared behavior

The applications use WCH USB-to-serial hardware, `pyserial`, 1.5 Mbaud, 8-N-1, internal/external triggering, `FF AA` acquisition, `FF FF` exposure updates, and `FF 09` correction-data retrieval.

## Critical differences

The S11639 source allocates 4118 raw bytes, decodes 2054 words, and displays words `6:2054`. The C12880MA package decodes 294 words and displays words `6:294`. Reusing the S11639 constants would block waiting for data the C12880MA can never produce.

The source also reads thousands of correction words with a two-second timeout per word. If the controller does not answer, that thread can appear frozen for a long time. The clean implementation uses one bounded frame deadline and reports the exact byte count.

## Present interpretation

Repeated all-zero fragments are not a valid dark spectrum. A protocol mismatch normally produces silence; recurring zero bytes can also indicate that the CH343 UART receive line is held low or that `COM5` is not the intended controller. This is evidence to investigate power, reset, controller-to-head cabling, and port identity, not proof that a new C12880MA sensor is defective.
