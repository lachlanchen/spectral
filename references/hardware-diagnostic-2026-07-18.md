# Hardware Diagnostic - 2026-07-18

## Detected serial devices

| Port | Identity | Interpretation |
| --- | --- | --- |
| `COM5` | WCH CH343, `1A86:55D3`, serial `5B90155911` | Probable C12880 controller |
| `COM3` | STMicroelectronics `0483:5740` | Existing STM32 USB CDC; not probed |
| `COM6` | Espressif `303A:1001` | Existing ESP32-S3; not probed |

## Probe result

`COM5` opens successfully at 1.5 Mbaud. Bare `FF AA`, the older six-byte manual packet, and the recovered nine-byte vendor packet were tested. Repeated requests returned only 0-55 zero bytes; a valid frame requires 588 bytes and should contain nonzero offset/noise even in darkness.

This proves USB enumeration and port opening, but not communication with the controller MCU or C12880MA head. No measured spectrum was available at the time of this diagnostic.

## Physical checks

1. Confirm the 15 cm cable is fully seated between controller and spectrometer head and is not reversed.
2. Press the controller's reset button once after USB connection.
3. Confirm the board receives USB 5 V and that its power indicator is stable.
4. Re-run `scripts\probe.ps1`.
5. A successful result reports `valid: true`, `pixels: 288`, and nonzero minimum/maximum values.

