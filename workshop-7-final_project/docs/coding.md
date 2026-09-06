# Coding Conventions

Rules for all firmware under `firmware/`. The design these rules
serve is in [`architecture.md`](./architecture.md); the pin map is in
[`interfaces.md`](./interfaces.md).

This is an **ESP-IDF + FreeRTOS** project. There is no `setup()` and no `loop()`.
Every path runs inside a task from the table in
[`architecture.md` §2](./architecture.md#2-task-architecture--aim).

## General

- **DO** keep every function at a single level of abstraction.
- **DO** apply SOLID rules to classes and functions.
- **DO** use meaningful variable names.
- **DON'T** use "magic" values. Named constants live in `Config.hpp` or
  `Pinout.hpp`.
- **DON'T** write verbose or obvious comments. Comment *why*, never *what*.
- **DO** use fixed-width types (`uint32_t`, `int16_t`) for anything that crosses
  a wire, a queue or a stored record.

## Tasks and timing

- **DON'T** block in `ctrl` or `safety`. `logger` is the only task allowed a long
  block, because an SD card can stall a single write for 100–250 ms.
- **DO** time periodic tasks with `vTaskDelayUntil`, not `vTaskDelay`. It absorbs
  a slow step instead of letting the period drift.
- **DON'T** use `vTaskDelay` as a substitute for waiting on an event. Block on the
  queue or the notification that actually carries the event.
- **DO** give every queue send and receive an explicit timeout. `portMAX_DELAY`
  is allowed only where blocking forever is the intended behaviour.
- **DO** keep `log_q` non-blocking: drop-oldest with a counter, never
  block a producer.

## Interrupts

- **DON'T** allocate memory, take a mutex, or call a logging function in an ISR.
- **DO** keep ISRs to a flag or a notification. The E-stop ISR calls
  `vTaskNotifyGiveFromISR` and nothing else.
- **DO** mark ISR handlers and anything they call `IRAM_ATTR`.
- **DO** use the `...FromISR` variant of every FreeRTOS call inside an ISR, and
  yield on the `pxHigherPriorityTaskWoken` it sets.
- **DO** mark variables shared between an ISR and a task `volatile`, and use an
  atomic type where the access is wider than a word.

## Memory

- **DO** allocate statically: `xTaskCreateStatic`, `xQueueCreateStatic`.
- **DON'T** call `malloc` or `new` after initialisation, and never in an ISR.
- **DO** install `vApplicationMallocFailedHook` and
  `vApplicationStackOverflowHook`. A silent overflow is worse than a reboot.
- **DO** check `uxTaskGetStackHighWaterMark` per task and log it at 1 Hz.

## Error handling

- **DO** use `ESP_ERROR_CHECK` for **initialisation invariants only** — a failure
  there means the board is misconfigured and aborting is correct.
- **DON'T** use `ESP_ERROR_CHECK` on a runtime call that can fail for a reason the
  system is designed to survive. It calls `abort()`, and an SD write error must
  never reboot a tracking gimbal. Handle those returns explicitly, count them, and
  keep the control loop running — see the four SD conditions in
  [`interfaces.md` §4](./interfaces.md#4-spi2--micro-sd).
- **DO** validate every value from a wire before use: reject `NaN` and `inf`
  explicitly with `isfinite()`, then range-check. See
  [`protocol.md` §2.3](./protocol.md#23-range-checks-44).

## Shared state

- **DO** guard shared configuration with a mutex.
- **DON'T** hold a mutex across a PID step or any other long computation. `ctrl`
  takes a local copy at the top of the step and releases immediately.
- **DO** name, in [`architecture.md`](./architecture.md), every variable that
  crosses a task boundary and what protects it.

## GPIO and peripherals

- **DO** check pin numbers against [`interfaces.md` §1](./interfaces.md#1-pin-map)
  before writing GPIO code. That table is authoritative, not the code.
- **DO** set a pin's level with `gpio_set_level()` **before** `gpio_config()`
  makes it an output. `gpio_config()` enables the output first, which briefly
  drives the reset-state level — on the laser gate that lights the beam.
- **DON'T** use ADC2 anywhere. It is unusable while WiFi is on, and WiFi arrives
  in Phase 1.

## Configuration and secrets

- **DO** put runtime-settable values in the config plane
  ([`architecture.md` §5](./architecture.md#5-configuration-and-storage)), so they
  are validated, persisted to NVS and acknowledged.
- **DON'T** hardcode WiFi or MQTT credentials. They belong in NVS or in a
  git-ignored header that is never committed. Broker credentials stay out of
  [`mqtt/config/`](../mqtt/config) in the repository.

## Build

- **DO** run `pio run` after changing dependencies in `platformio.ini`.
- **DO** keep the build self-contained. Nothing in
  [`workshop-7-final_project/`](../) may reference a path outside it — sources are
  copied in, never linked across workshop folders.
