# Dual-H7 Light Modulation and Spectral Acquisition

## Scope and current status

The existing `aginti_c12880_h743` code is the clean-room replacement firmware
for the current C12880MA controller. It drives `ST/CLK`, samples the analog
video through ADC1, and streams frames over USB. It does not modulate lamps.

The new `aginti_dual_h7_coordinator` target is for a separate Geek
STM32H743IIT6 board. It controls illumination and schedules acquisition. Both
targets remain compile-only until explicit permission is given to flash.

## Recommended architecture

```text
PC experiment controller
  | USB CDC: spectra                         | ST-Link VCP: commands/telemetry
  v                                          v
C12880 controller H7 <--- trigger PG4->PE9 --- Coordinator H7
  | ST/CLK/ADC             UART metadata      | TIM2 CH1/CH2, same counter
  v                       <-------------------+ PA0 -> MOS1 -> lamp1
C12880MA                                       PA1 -> MOS2 -> lamp2
                                               PB8/PB9 -> INA219 x2,
                                                          TSL2591, AS7343
```

Use two MCUs because acquisition clock/ADC/USB and lamp/telemetry/display work
have different latency requirements. The spectrometer MCU must never wait for
a 100 ms ambient-light integration or display drawing. The coordinator must
never generate sensor CLK edges in a software loop while also servicing lamp
safety.

## Wiring contract

| Coordinator signal | Destination |
|---|---|
| `PA0/A0 TIM2_CH1` | YYNMOS-1 lamp 1 `PWM` |
| `PA1/A1 TIM2_CH2` | YYNMOS-1 lamp 2 `PWM` |
| STM32 `GND` | both MOS `GND`, sensor ground, C12880-controller ground |
| `PB8/B8` | shared `SCL` |
| `PB9/B9` | shared `SDA` |
| `PD8 TX` | future C12880-controller UART `RX` |
| `PD9 RX` | future C12880-controller UART `TX` |
| `PG4` | proposed C12880-controller `PE9` external-trigger input |

`PG4 -> PE9` is not yet physically verified on the spectrometer PCB. Confirm
the PE9 pad/header with its schematic or continuity measurement before making
that wire. Never connect a 5 V trigger to PE9. Do not route lamp current
through an STM32 ground pin; join grounds as a logic reference near the power
entry and keep the load-current loop separate.

The observed I2C addresses are `TSL2591=0x29`, `AS7343=0x39`, and the two
INA219 channels at `0x40/0x41`, so a TCA9548A is not required unless the bus
is electrically unstable. Verify the actual monitor HAT addresses because a
previous H7 probe did not detect those two channels.

## Why 16-bit control is useful but not magic

The YYNMOS-1 seller specification limits its PWM input to 500 Hz. TIM2 runs
both outputs from the same 32-bit counter at 400 Hz. At a 200 MHz timer clock,
one PWM period has about 500,000 timer ticks. The public command and LUT format
uses 0..65535, so it supplies true 16-bit duty commands while the timer has
additional internal margin.

Both CCR preload registers are written while update events are inhibited. A
single natural timer update then transfers both shadow values to the active
channels. This prevents the brief mismatched state produced by updating two
independent PWM peripherals one after another. It does not create analog
voltage. Tungsten thermal inertia performs most optical smoothing, and the
MOS/optocoupler, supply, filament, and optics still set the real resolution.

## Deterministic sequence

1. Stage `(d1,d2)` into TIM2 preload registers.
2. Wait for the shared PWM update interrupt to confirm simultaneous commit.
3. Wait the configured optical settling interval.
4. Emit a 10 us hardware-trigger pulse and a UART metadata packet.
5. The C12880 controller captures one spectrum and USB streams it.
6. Slow telemetry is sampled independently and cannot delay PWM commit.

The LUT is parameterized by normalized phase, not milliseconds. The same LUT
therefore runs at 0.1, 0.5, 1, 3, 5, or 10 seconds. The coordinator reduces
the effective number of points when the requested period is too short for the
400 Hz PWM boundary plus settling delay. At 400 Hz, a 100 ms cycle contains at
most 40 distinct PWM periods. A tungsten filament may not reach the same
thermal endpoints in 100 ms as it does in 3 s, so period-specific calibration
or a thermal state model is still necessary.

## Calibration objective

For measured lamp response functions `I_1(d_1)`, `I_2(d_2)` and spectral
vectors `s_1(d_1)`, `s_2(d_2)`, select a path through duty-pair space that
minimizes

```text
J = wI * sum_k ((I1(d1k)+I2(d2k)-I0)/I0)^2
  + wS * sum_k ||normalize(s1+s2)-target_shape(k)||^2
  + wD * sum_k ||d(k)-d(k-1)||^2.
```

The endpoint spectral shapes are chosen far apart along the dominant spectral
variation direction while candidate duty pairs remain close to target
intensity. `tools/optimize_dual_lamp_lut.py` generates a closed forward/reverse
path and a C header. Runtime firmware only interpolates this LUT, which is
deterministic and substantially faster than solving an optimization problem
on every sample.

Electrical power is not optical intensity. INA219 is for electrical safety and
drift diagnostics. TSL2591 provides broadband/IR-assisted intensity but its
integration time is too slow for fast inner-loop control. AS7343 supplies a
compact multispectral feature vector; its 18 data registers are multiplexed
through six ADCs, so they are not 18 perfectly simultaneous spectrometer bins.
The C12880MA remains the primary spectrum measurement.

## Firmware improvement plan

### Implemented now

- Separate coordinator target with safe idle, arm/run/stop, fault and cooling states.
- Shared TIM2 preload commit for two 16-bit duty commands at 400 Hz.
- Period-independent LUT interpolation and automatic effective-step reduction.
- Proposed `PG4 -> PE9` trigger pulse plus CRC-protected UART metadata packet.
- Read-only AS7343/TSL2591/INA219 telemetry and electrical safety limits.
- Host CLI and offline flat-intensity/high-spectral-span LUT optimizer.

### Next hardware-validation stage

- Confirm HSE, PWM frequency, PA0/PA1 voltage, and simultaneous edges by scope.
- Confirm PE9 accessibility, polarity, trigger threshold, and common ground.
- Verify INA219 addresses and shunt value before trusting current in mA.
- Capture single-lamp calibration at the final 5 V supply and final optical alignment.
- Compare C12880 frame sequence with coordinator trigger sequence.

### C12880 acquisition upgrade after first validation

- Convert PRE/ST/integration/dummy/ADC stages to a fully interrupt-driven DMA state machine.
- Timestamp PE9 in the EXTI ISR and arm timer/ADC hardware without main-loop polling.
- Add modulation index, trigger timestamp, and sync CRC status to a V3 frame header.
- Raise sensor CLK toward 5 MHz only after ADC phase and analog settling pass scope tests.
- Keep V2 and the vendor-compatible 590-byte frame as fallback modes.

## Build and operation

```powershell
./scripts/build_dual_h7_coordinator.ps1
python firmware/sdk/tools/coordinator_cli.py --port COM7 status
python firmware/sdk/tools/coordinator_cli.py --port COM7 upload-lut calibration/lut.csv
python firmware/sdk/tools/coordinator_cli.py --port COM7 run --cycles 1 --period-ms 3000
python firmware/sdk/tools/coordinator_cli.py --port COM7 stop
```

The build script compiles only. It contains no erase, program, reset, or
OpenOCD command.

## Primary references

- ST AN4776, *General-purpose timer cookbook for STM32 microcontrollers*.
- ST RM0433 and DS12110 for STM32H743 timers, GPIO, DMA, ADC, and serial interfaces.
- Hamamatsu C12880MA/C16767MA datasheet and timing diagram.
- ams OSRAM AS7343 and TSL2591 datasheets.
- Texas Instruments INA219 datasheet.
