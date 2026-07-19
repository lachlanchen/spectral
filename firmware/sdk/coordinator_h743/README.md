# Dual-H7 Coordinator Firmware

This target turns the separate Geek STM32H743IIT6 board into a deterministic
dual-lamp and acquisition coordinator. It does not replace or overwrite the
C12880MA acquisition target.

## Fixed pin contract

| Function | Pin |
|---|---|
| Lamp 1 YYNMOS PWM | `PA0 / A0 / TIM2_CH1` |
| Lamp 2 YYNMOS PWM | `PA1 / A1 / TIM2_CH2` |
| Shared sensor SCL | `PB8 / B8` |
| Shared sensor SDA | `PB9 / B9` |
| Host VCP UART | `PA9 TX`, `PA10 RX`, 115200 baud |
| Spectrometer metadata link | `PD8 TX`, `PD9 RX`, 1 Mbaud |
| Spectrometer trigger | `PG4`, 3.3 V pulse |

The `PG4 -> C12880 controller PE9` path is a proposed dedicated trigger. Do
not connect it until PE9 access and ground continuity have been confirmed on
the actual C12880 controller PCB. All boards and MOS input grounds must share
one logic reference; lamp power never enters an STM32 pin.

## Safety

Boot and fault states force both duties to zero. A run requires `ARM`, stops
after its requested cycle count, and enters a five-second cooling state. The
default maximum continuous active time is ten seconds. INA219 readings, when
present, add current and total-power trips. Missing monitors are reported and
must not be mistaken for active electrical protection.

The 128-point built-in LUT is only a seed copied from an earlier 5 V optical
alignment. Recalibrate after any lamp, supply, diffuser, lens, beamsplitter,
sensor position, or optical-path change.
