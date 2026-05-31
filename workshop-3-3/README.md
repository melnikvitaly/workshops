# Workshop 3-3 Power and Speed Control: How to Change Light Brightness and Motor RPM

Task

A potentiometer controls the brightness of an LED.
A potentiometer controls the speed of a motor shaft.
The motor and the LED do not affect each other's operation.

## Demo

[Video](https://drive.google.com/file/d/1s5af2nec6LzN5YVUpVkG88OlhpT7V85x/view?usp=drive_link)

## Solution Key Points

- **ADC → percent**: raw ADC reading is smoothed (SMA-10) then converted to 0–100% via `adc.pct()` — [`ADC.hpp`](src/hardware/ADC.hpp), [`Sma.hpp`](src/Sma.hpp)
- **LED**: percent maps directly to PWM duty (0–100%), GPIO 8 — [`Led.hpp`](src/hardware/Led.hpp)
- **Motor**: percent maps to a configurable range with three zones (GPIO 35) — [`Motor.hpp`](src/hardware/Motor.hpp):
  - below `MOTOR_CUT_OFF` (5%) → fully off
  - above `MOTOR_CUT_ON` (95%) → fully on
  - in between → linearly scaled from `MOTOR_MIN_PCT` (65%) to `MOTOR_MAX_PCT` (100%)
- **Observed range**: `Ranges` tracks the live ADC min/max, visible in the serial log — [`Ranges.hpp`](src/Ranges.hpp)
- **PWM**: 20 kHz on both outputs — above audible range, no motor whine — [`PWM.hpp`](src/hardware/PWM.hpp)
- **Wiring and constants**: [`main.cpp`](src/main.cpp)`