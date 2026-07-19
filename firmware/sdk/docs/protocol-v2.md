# AgInTi Spectrometer Protocol V2

All multibyte fields are little-endian. CRC is IEEE CRC-32 over the complete
record with its CRC field set to zero.

## Command header

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | ASCII `ASC2` |
| 4 | 1 | version, currently 2 |
| 5 | 1 | opcode |
| 6 | 2 | flags |
| 8 | 4 | sequence |
| 12 | 2 | payload bytes, maximum 64 |
| 14 | 2 | reserved |
| 16 | 4 | CRC-32 |
| 20 | N | payload |

Opcodes are `HELLO=0x01`, `GET_CAPS=0x02`, `CONFIGURE=0x10`,
`SINGLE_SHOT=0x11`, `START_STREAM=0x12`, `STOP_STREAM=0x13`,
`GET_STATUS=0x14`, and read-only `EEPROM_READ=0x20`.

`CONFIGURE` carries `<uint32 clock_hz, uint32 exposure_clocks, uint8 mode,
uint8 frame_format, uint16 reserved>`. Frame format 0 is legacy and 1 is V2.

## Reply header

Replies use ASCII `ASR2`, version 2, opcode with bit 7 set, a 16-bit status,
the request sequence, payload length, and CRC-32. The header is 20 bytes.

## V2 spectrum frame

| Offset | Bytes | Field |
|---:|---:|---|
| 0 | 4 | ASCII `ASP2` |
| 4 | 1 | version 2 |
| 5 | 1 | header bytes, 48 |
| 6 | 2 | flags; bit 0 means little-endian samples |
| 8 | 4 | sequence |
| 12 | 4 | payload bytes, normally 588 |
| 16 | 8 | capture-complete timestamp in microseconds |
| 24 | 4 | integration clock count |
| 28 | 4 | actual sensor clock in hertz |
| 32 | 2 | active pixels, 288 |
| 34 | 2 | acquired raw samples, 294 |
| 36 | 4 | status bits |
| 40 | 4 | cumulative dropped frames |
| 44 | 4 | CRC-32 |
| 48 | 588 | 294 unsigned 16-bit raw samples |

The first 288 samples are active pixels. Samples 288 through 293 are retained
as trailer/flush diagnostics. A host can discard them after validating timing.

## Legacy compatibility

The parser recognizes `04 0D 0A` identity requests and 9-byte commands of the
form `FF opcode mode exposure_le32 0D 0A`. A normal acquisition returns 590
bytes: 12 zero bytes followed by 289 little-endian words. EEPROM write opcodes
are intentionally disabled in the clean-room firmware.

