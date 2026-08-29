# Workshop 6-1 (ДЗ): суперлуп → задачі FreeRTOS

## Завдання

Виконати ДЗ з уроку 2.3 (`../../workshop-2-3`), але використовуючи функціонал
RTOS, а саме `xTaskCreate` або `xTaskCreatePinnedToCore`.

## Behavior (unchanged from 2-3)

| Component                  | Behavior                                                    |
| -------------------------- | ----------------------------------------------------------- |
| **Button** (GPIO 5)        | Press triggers alert sequence                               |
| **LED1** (GPIO 4)          | Turns ON solid for **5 seconds** after button press (alert) |
| **LED2** (GPIO 3)          | Blinks for **3 seconds** immediately after LED1 alert ends  |
| **LED3** (GPIO 2)          | Brightness controlled by potentiometer via PWM (0–100%)     |
| **Potentiometer** (GPIO 0) | ADC1 channel 0, maps to LED3 brightness                     |

```
Button press
    └─> LED1 ON (5s alert)
            └─> LED1 OFF, LED2 blinks (3s)
                        └─> LED2 OFF, idle
```

LED3 runs independently — the potentiometer controls brightness at all times.

## What changed against 2-3

The behaviour is identical; only the scheduling is different.

| 2-3 (superloop)                     | 6-1 (FreeRTOS)                                    |
| ----------------------------------- | ------------------------------------------------- |
| `loop()` calls every `tick()`       | one task per `tick()`, created with `xTaskCreate` |
| all ticks run at the same rate      | each task picks its own period                    |
| `delay()` forbidden                 | `xTaskDelayUntil()` — blocks the task, not the CPU |
| `millis()`                          | `nowMs()` over `esp_timer_get_time()`             |
| `LoopTracker` measures loop rate    | `statsTask` reports per-task rate + stack headroom |
| Arduino framework, ESP32-C3         | ESP-IDF 6.0, ESP32-C3                             |

| Task     | Period | Prio | Does                                       |
| -------- | ------ | ---- | ------------------------------------------ |
| `button` | 5 ms   | 5    | `btn.tick()` — debounce, fires `onPress`   |
| `alert`  | 10 ms  | 4    | `alert.tick()`, `led1.tick()`, `led2.tick()` |
| `pot`    | 50 ms  | 3    | `led3.power(pot.percent())`                |
| `stats`  | 5 s    | 1    | logs iterations/period and stack high-water |

Priorities follow deadline tightness: the button must not miss a press, the
potentiometer can be sampled lazily, statistics run on whatever is left over.

`xTaskCreate` rather than `xTaskCreatePinnedToCore`: the C3 is single-core, so
there is no second core to pin to. On a dual-core part (S3) the same three calls
become `xTaskCreatePinnedToCore(..., APP_CPU_NUM)`.

## Known trade-off: shared state without IPC

`buttonTask` calls `alert.onButtonPress()` directly, so `AlertController::_state`
and `_stateStartedAt` are written by the button task and read by the alert task
with no mutex or queue between them. This is the deliberate "tasks only" shape of
this homework, and it is safe enough in practice here — the button task is higher
priority, the writes are single-word, and `IDLE` is the only state
`onButtonPress()` acts on — but it is not race-free by construction: the alert
task can be preempted between its `_state` check and its `_state` write.

The proper fix is a queue: `buttonTask` posts an event, `alertTask` owns the state
machine outright and is the only writer. That is one `xQueueCreate` and a
`xQueueReceive` with a timeout in place of `xTaskDelayUntil`.

## Build

```
pio run                 # build
pio run -t upload       # flash
pio device monitor       # 115200 baud
```

Board: `esp32-c3-devkitm-1`, framework `espidf` (IDF 6.0.1),
`CONFIG_FREERTOS_HZ=1000` so that the 5 ms button period is one tick.

## Wiring

| Signal    | GPIO | Notes                                        |
| --------- | ---- | -------------------------------------------- |
| LED1      | 4    | LED + resistor to GND, LEDC ch1 / timer1     |
| LED2      | 3    | LED + resistor to GND, LEDC ch2 / timer1     |
| LED3      | 2    | LED + resistor to GND, LEDC ch0 / timer0     |
| Button    | 5    | to GND, internal pull-up, active LOW         |
| Pot wiper | 0    | ADC1_CH0, 12-bit, 12 dB atten (full 0–3.3 V) |

GPIO2 is a strapping pin on the C3 — it must not be held low at reset. A plain
LED to GND through a resistor is fine; the LEDC output is only driven after boot.

## Files

```
src/main.cpp              tasks, pins, wiring of the objects
src/Config.hpp            durations + task periods
src/Clock.hpp             nowMs() — the millis() replacement
src/AlertController.hpp   the 5s/3s state machine (unchanged logic)
src/hardware/Debug.hpp    scoped logger over ESP_LOG
src/hardware/PWM.hpp      LEDC wrapper, 20 kHz / 10-bit
src/hardware/Led.hpp      on / off / power / blink
src/hardware/Button.hpp   polled debounce, press / release / long-press
src/hardware/ADC.hpp      adc_oneshot + hysteresis
```
