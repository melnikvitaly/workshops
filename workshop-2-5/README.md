# Workshop 2-5. Timers

Use ESP-IDF framework (ESP32-C3 DevKitM-1).

## Task

Implement exhaust fan control that automatically turns on for 15 minutes every hour, then shuts off until the next cycle — all driven by timers, not polling or delays.

**Fan output:** GPIO driving a motor driver (L298N) or relay.

## Common Requirements (all subtasks)

- No `vTaskDelay()` or busy-wait `delay()` to implement timing logic
- On-duration and cycle period defined as named constants in `Config.h`:
  ```c
  FAN_PERIOD_US = 8'000'000   // 8 s
  FAN_ON_US     = 4'000'000   // 4 s
  ```
- Fan state logged via `Debug::print()` on every transition (`"Fan ON"` / `"Fan OFF"`)
- `app_main` initializes the timer(s), then enters the existing `while(true)` loop
- To switch subtasks change the `#include` in `main.cpp` (e.g. `#include <Task2.h>`)

---

## Subtask 1 — FreeRTOS Software Timers

Highest abstraction. Two timers (periodic + one-shot) created with `xTimerCreate`. Callbacks run in the shared Timer Daemon task — defer GPIO and logging to `loop()` via a flag.

Implement from scratch in `Task1.h`. **Key header:** `freertos/timers.h`

---

## Subtask 2 — `esp_timer`

Same two-timer structure as Subtask 1, but backed by a dedicated high-priority ESP-IDF task instead of the shared daemon. Callbacks can call GPIO and logging directly.

Implement from scratch in `Task2.h`. **Key header:** `esp_timer.h`

---

## Subtask 3 — Hardware Timer (`gptimer`)

One hardware timer, ISR callback. Alternates `alarm_count` between `FAN_ON_US` and `FAN_OFF_US` on each fire. Cannot call GPIO/logging from ISR — signals `loop()` via `xTaskNotifyFromISR`.

Implement from scratch in `Task3.h`. **Key header:** `driver/gptimer.h`

---

## Subtask 4 — Direct Register Access

Same logic as Subtask 3 but without the gptimer driver. Writes `TIMERG0` registers directly: configure divider, set alarm, clear interrupt flag, re-arm — all manually.

Implement from scratch in `Task4.h`. **Key header:** `soc/timer_group_struct.h`, `esp_intr_alloc.h`

---

## Comparison

|                               | Subtask 1 — FreeRTOS timers             | Subtask 2 — esp_timer            | Subtask 3 — gptimer          | Subtask 4 — Registers        |
| ----------------------------- | --------------------------------------- | -------------------------------- | ---------------------------- | ---------------------------- |
| **Abstraction**               | Highest                                 | High                             | Low                          | None                         |
| **Callback context**          | Timer Daemon task                       | Dedicated ESP-IDF task           | ISR                          | ISR                          |
| **GPIO/log safe in callback** | Risky (limited stack) — defer to loop() | Yes — defer to loop() also works | No — ISR; must notify loop() | No — ISR; must notify loop() |
| **Resolution**                | FreeRTOS tick (~1 ms)                   | ~1 µs                            | 1 µs (hardware)              | 1 µs (hardware)              |
| **Number of timers used**     | 2 (period + one-shot)                   | 2 (period + one-shot)            | 1 (alternating alarm)        | 1 (alternating alarm)        |
| **Max timers available**      | Unlimited (heap-bound)                  | Unlimited (heap-bound)           | 2 (ESP32-C3 hardware)        | 2 (ESP32-C3 hardware)        |
| **Interrupt flag clearing**   | N/A                                     | N/A                              | Driver                       | Manual                       |
| **Interrupt allocation**      | N/A                                     | N/A                              | Driver                       | `esp_intr_alloc()` manually  |
| **Alarm re-arm**              | N/A                                     | N/A                              | `gptimer_set_alarm_action()` | `alarm_en = 1` manually      |
| **Chip portability**          | Full                                    | Full                             | Full                         | ESP32-C3 specific            |
| **Overhead**                  | Highest                                 | Medium                           | Low                          | Lowest                       |

---

## Optional Enhancements (any subtask)

> Implemented in `Task1.h` as a reference.

- Watchdog timer (`esp_task_wdt`) to reset if firmware hangs
- State transition logging: print timestamp and elapsed duration on each `"Fan ON"` / `"Fan OFF"` via `Debug::print()`
- Guard against re-entry: ignore a new period trigger if the fan is already ON
- LED indicator mirroring current fan state
