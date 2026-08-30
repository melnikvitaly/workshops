# Workshop 6-1 (ДЗ): суперлуп → задачі FreeRTOS. Чому не можна писати все в superloop: Проблема керування часом і багатозадачність

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
| callback ran inline in `loop()`     | button notifies, `alertTask` owns the state        |
| `delay()` forbidden                 | `xTaskDelayUntil()` / `xTaskNotifyWait()` — blocks the task, not the CPU |
| `millis()`                          | `nowMs()` over `esp_timer_get_time()`             |
| `LoopTracker` measures loop rate    | `statsTask` reports per-task rate + stack headroom |
| Arduino framework, ESP32-C3         | ESP-IDF 6.0, ESP32-C3                             |

| Task     | Period | Prio | Does                                       |
| -------- | ------ | ---- | ------------------------------------------ |
| `button` | 5 ms   | 5    | `btn.tick()` — debounce, notifies `alert` on press |
| `alert`  | 10 ms* | 4    | owns the state machine; woken by `AlertEvent` bits |
| `pot`    | 50 ms  | 3    | `led3.power(pot.percent())`                |
| `stats`  | 5 s    | 1    | logs iterations/period and stack high-water |

\* `alert` blocks on `xTaskNotifyWait` with a 10 ms timeout rather than a
fixed delay, so 10 ms is its idle period, not a fixed one — a press wakes it
sooner.

Priorities follow deadline tightness: the button must not miss a press, the
potentiometer can be sampled lazily, statistics run on whatever is left over.

`xTaskCreate` rather than `xTaskCreatePinnedToCore`: the C3 is single-core, so
there is no second core to pin to. On a dual-core part (S3) the same three calls
become `xTaskCreatePinnedToCore(..., APP_CPU_NUM)`.

## Ownership: the button signals, it does not call

`AlertController` belongs to `alertTask` — that task is the only code that ever
touches `_state` or `_stateStartedAt`. `buttonTask` does not reach into it; on a
press it sends a direct-to-task notification and returns:

```
buttonTask                              alertTask
  btn.tick()                              xTaskNotifyWait(0, ALL, &events, 10ms)
    onPress ──xTaskNotify(────────────▶     └─ BUTTON_PRESS → onButtonPress()
              BUTTON_PRESS, eSetBits)       └─ timeout      → (nothing)
                                          alertController.tick(); led1/led2.tick()
```

The notification word is used as **event bits**, not as a counter: each event
gets a name (`AlertEvent::BUTTON_PRESS`), so `alertTask` learns *which* event
arrived rather than only that something did. Adding a second producer — a long
press, a UART command, a timer — costs one more bit and one more `if`, with no
change to the channel. `xTaskNotifyGive`/`ulTaskNotifyTake` are the same
primitive in counting-semaphore mode and could only have said "N somethings".

`xTaskNotifyWait(0, UINT32_MAX, …)` — clear nothing on entry, so bits that
arrived while the task was busy survive; clear everything on exit, so each event
is consumed exactly once. Bits are only cleared when a notification was really
received, so the **return value**, not the bit pattern, is what says whether
`events` is fresh. Repeated presses coalesce into one set bit, which matches
`onButtonPress()` ignoring anything that arrives while the sequence is not
`IDLE`.

Beyond 32 events, multiple consumers of the same event, or an event that needs a
payload, this stops being enough — that is what `EventGroupHandle_t` and queues
are for.

Two things follow. Ownership is now single-writer **by construction**, not by
luck — no mutex needed, because no state is shared. And the press latency drops
from "up to one 10 ms alert period" to "the next context switch", because the
notification wakes the task instead of waiting for its next scheduled tick.

Why a task notification rather than a queue or a binary semaphore: there is one
consumer and no payload. A notification is a single word in the
receiving task's TCB — no allocation, and roughly an order of magnitude cheaper
than the equivalent queue operation. A queue would be the right call the moment the event needs to carry data
(which button, how long it was held) or several presses must be buffered.

`alertTask` is created before `buttonTask` on purpose: `buttonTask` has a higher
priority than `app_main`, so it preempts it as soon as it exists, and its notify
target must already be there.

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
| Pot wiper | 0    | ADC1_CH0, 12-bit, 12 dB atten (widest range) |

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
