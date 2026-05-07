# Workshop 2-3: Superloop та незалежні задачі

## Task

Control three independent processes in one `loop()` without blocking.

## Behavior

| Component                  | Behavior                                                    |
| -------------------------- | ----------------------------------------------------------- |
| **Button**                 | Press triggers alert sequence                               |
| **LED1** (GPIO 4)          | Turns ON solid for **5 seconds** after button press (alert) |
| **LED2** (GPIO 5)          | Blinks for **3 seconds** immediately after LED1 alert ends  |
| **LED3** (GPIO 6)          | Brightness controlled by potentiometer via PWM (0–100%)     |
| **Potentiometer** (GPIO 0) | Analog input, maps to LED3 brightness                       |

## Sequence

```
Button press
    └─> LED1 ON (5s alert)
            └─> LED1 OFF, LED2 blinks (3s)
                        └─> LED2 OFF, idle
```

LED3 runs independently — potentiometer controls brightness at all times.

## Rules

- No `delay()`
- No `while()` in `loop()`
- All timing via `millis()`

## Config

| Constant            | Value | Description          |
| ------------------- | ----- | -------------------- |
| `ALERT_DURATION_MS` | 5000  | LED1 on duration     |
| `BLINK_DURATION_MS` | 3000  | LED2 blink duration  |
| `BLINK_INTERVAL_MS` | 200   | LED2 toggle interval |
| `DEBOUNCE_MS`       | 20    | Button debounce      |
