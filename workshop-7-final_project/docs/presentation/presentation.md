# Presentation Draft — Laser Gimbal with Camera Tracking

Draft for the slide template. One `##` header = one slide.
Diagram sources are in [`diagrams/`](./diagrams/) (Mermaid → SVG).

---

## 1. Title

Laser Gimbal with Camera Tracking on ESP32-S3 (FreeRTOS, PID, OpenCV)

| Item | Value |
| --- | --- |
| Student | Vitaly Melnik |
| Group | TODO |
| Course | Embedded Development |
| Defence date | TODO |
| GitHub | TODO: link |

---

## 2. Hook

> The laser dot is sure to be off target.
> The camera only sees it **50–250 ms later**.
> How do you steer something you see late?

---

## 3. Problem

| Item | Value |
| --- | --- |
| Task | Put a laser dot on a target, using only a camera as feedback |
| Why hard | Camera → vision → link delay is 50–250 ms. This delay sets the gain limit |
| Why hard | Servos have deadband, backlash and gravity droop |
| Safety | A laser must never turn on by mistake |
| Limits | One UART link, ESP32-S3, PC does the vision |

---

## 4. Solution — big picture

![Data flow](./diagrams/data-flow.svg)

| Node | Job |
| --- | --- |
| `EYE` (PC) | Sees the laser dot and the target. Sends the error vector |
| `AIM` (ESP32-S3) | Runs 2 PIDs, drives servos, logs to SD |

- Camera is the real feedback. It does not trust the servo angle.
- One UART link, 115200 baud — lowest latency.

---

## 5. Why velocity, not position

- Servo **velocity** → angle → dot position = an integrator, `P(s) = k/s`.
- A PID on an integrator is simple to tune.
- Camera feedback fixes droop, deadband and backlash.
- Delay, not mechanics, limits the gain.

TODO: one slide picture — error vs time, before/after gain tuning.

---

## 6. Architecture — FreeRTOS tasks

![Tasks](./diagrams/tasks.svg)

| Task | Core | Note |
| --- | --- | --- |
| `ctrl` | 1 | 20 ms fixed period, PID + servos |
| `safety` | 1 | Highest priority. E-stop and laser gate |
| `link_uart` | 0 | Reads UART, fills `cmd_q` |
| `logger` | 0 | Lowest priority. Only task that may block long |
| `ui` | 0 | OLED + MODE button |

Why: an SD card can stall a write for 100–250 ms. That must never reach `ctrl`.

Source: [`architecture.md`](../architecture.md#2-task-architecture--aim)

---

## 7. Safety — the laser gate

![States](./diagrams/states.svg)

- Laser is allowed only in `ZONE_TOUR` and `ARMED`.
- `laserPermitted()` needs **all**: `ARMED`, link fresh, watchdog OK,
  no E-stop, beam requested.
- No data for 300 ms → `LINK_LOST`, motors stop.
- E-stop: ISR → task notification → `safety`. No work in the ISR.
- Source: [`StateMachine.hpp`](../../firmware/aim/src/StateMachine.hpp)

---

## 8. Implementation — key decisions

| # | Decision | Why |
| --- | --- | --- |
| 1 | Only one input channel at a time: `AUTO` / `MANUAL` / `NONE` | No hidden mixing. Switch resets PIDs, so no jump |
| 2 | Two rate limits: hard limit in `Gimbal`, PID clamp below it | Anti-windup can see the real limit (`static_assert`) |
| 3 | Log queue drops oldest, never blocks | Telemetry may be lost. Control may not |
| 4 | NDJSON + CRC-8, 256-byte line limit, `NaN` rejected | A `NaN` in the PID would break the integrator forever |
| 5 | Boot zone tour: laser draws the zone clockwise | Wrong direction = wrong axis flag = runaway |

Code: [`Gimbal.hpp`](../../firmware/aim/src/parts/Gimbal.hpp),
[`Ctrl.hpp`](../../firmware/aim/src/tasks/Ctrl.hpp)

---

## 9. Implementation — code

TODO: pick one 10–20 line snippet.
Good candidates:

- PID clamp + `static_assert` in [`Gimbal.hpp`](../../firmware/aim/src/parts/Gimbal.hpp)
- `laserPermitted()` in [`Safety.hpp`](../../firmware/aim/src/tasks/Safety.hpp)

---

## 10. Problems we solved

| Problem | Fix |
| --- | --- |
| Log text on UART mixed with data; a line starting with `F` could fire | Data moved to UART1. UART0 is console only. Fire command hardened |
| Relay laser lit at power-on | Set pin level before making it an output, plus pull-up |
| TODO: biggest bug that cost most time | TODO |

---

## 11. Results

TODO: video (QR code).

| Metric | Value |
| --- | --- |
| Control step | 20 ms |
| Link timeout | 300 ms |
| Tracking error (px), steady | TODO |
| Settling time | TODO |
| SD max write latency | TODO |
| Soak run time without fault | TODO |

Conditions: TODO (camera, lighting, distance, gains).

TODO: graph from SD log — error vs time, "before / after" tuning.

---

## 12. What next

| Item | Value |
| --- | --- |
| Now | Closed loop over one UART link, SD log, OLED, E-stop |
| Next | TODO: most important improvement |
| Later | Wireless remote (`pilot`, ESP32-C3), MQTT, storage node (`vault`, STM32) |
