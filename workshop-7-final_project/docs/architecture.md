# Architecture — Laser Gimbal with Camera Tracking

The technical design of the system: what each node is responsible for, how `AIM`'s
tasks are structured, how the nodes talk, and the contracts that the control,
storage and safety paths hold to.

The project plan — phases, hardware deliverables, repository layout and the demo —
is in [`../README.md`](../README.md); the phased checklist is in
[`../TASKS.md`](../TASKS.md).

**Contents**

1. [Node responsibilities](#1-node-responsibilities)
2. [Task architecture — `AIM`](#2-task-architecture--aim)
3. [Communications](#3-communications)
4. [Input channels](#4-input-channels)
5. [Configuration and storage](#5-configuration-and-storage)
6. [Control](#6-control)
7. [Safety](#7-safety)

---

## 1. Node responsibilities

Each node has a **role name** that does not mention its silicon, so a node can be
re-hosted on different hardware without a rename cascade. The role table and the
namespace mapping are in [`../README.md`](../README.md#2-nodes).

### EYE — vision and operator console *(PC)*

- Detects the red laser dot and the black target dot, computes the error vector.
- Detection methods are encapsulated behind one interface:
  - **method 1 (Phase 0)** — OpenCV threshold + shape gate
  - **method 2 (Phase 2)** — CSRT tracker. Note this needs `opencv-contrib-python`
    (`cv2.TrackerCSRT_create()`), not the plain `opencv-python`; and it is a
    *tracker*, so it needs an initial box and periodic re-detection to correct drift.
- Streams the error vector to `AIM` over **UART1** or **MQTT**.
- Hosts the Mosquitto broker and the operator UI: gain presets, nudge, telemetry
  graph, fire button, and the configuration messages that select the input channel.
- A separate script drives the gimbal manually from the mouse.

### AIM — gimbal controller *(ESP32-S3; the core of the project)*

- Runs the two PIDs and drives the servos by velocity.
- Owns the **SD card** over SPI, and owns **timestamps** for every record.
- Renders state on a 0.96" I²C OLED: error, gains, aiming status, active input
  channel, link and storage health, CPU load.
- Publishes telemetry over WiFi/MQTT — **only when enabled in configuration**.
- Accepts commands from exactly one input channel at a time (§4).

### PILOT — wireless remote *(ESP32-C3; Phase 1)*

- 2-axis analog joystick controls gimbal velocity.
- **ESP-NOW** to `AIM` (primary). BLE and NRF24 are alternatives behind the same
  transport interface, Phase 1/2.
- Battery powered with real **deep sleep**: auto-sleep after inactivity, wake on the
  joystick button GPIO, counters preserved in RTC memory.
- Carries a **remote emergency stop** button. In Phase 0 the E-stop is the local
  button on the `AIM` board, which works with no radio at all — so deferring `PILOT`
  costs no safety, only convenience.

### VAULT — storage node *(STM32; Phase 1)*

Deliberately the thinnest node in the system: it owns the medium and nothing else.

- SPI **slave** to `AIM` (`AIM` is master), DMA, double-buffered.
- **No parsing, no timestamps, no state.** Records arrive fully formed and are
  written verbatim.
- SPI **master** to the SD card; FatFs and the rotation scheme move here from `AIM`.
- Reports card present / free space / write errors / current segment back to `AIM`.
- `IWDG`, refreshed only while the card and the link are both healthy.

---

## 2. Task architecture — `AIM`

### FreeRTOS task set — `AIM` (ESP32-S3)

| Task | Core | Prio | Period | Owns | Talks via |
|------|------|------|--------|------|-----------|
| `ctrl` | 1 | high | 20 ms, `vTaskDelayUntil` | PIDs, gimbal, servo PWM | reads `cmd_q`, writes `log_q` |
| `safety` | 1 | realtime | blocks on notify | E-stop, laser interlock, WDT arbiter | ISR → task notification |
| `link_uart` | 0 | normal | event | UART1 RX/TX, framing | writes `cmd_q` |
| `link_net` | 0 | normal | event | WiFi, MQTT, config plane | writes `cmd_q`, reads `tlm_q` |
| `radio` | 0 | normal | event | ESP-NOW from the joystick | writes `cmd_q` |
| `logger` | 0 | **low** | drains `log_q` | SD card, FatFs, rotation | reads `log_q` |
| `ui` | 0 | low | 100 ms | OLED render | reads shared state under mutex |

Rules that make this structure work, and that the write-up must state explicitly:

- **`cmd_q` is one queue with a tagged-union item.** Many producers, one consumer;
  producers never see each other.
- **`logger` is the lowest priority and the only task allowed to block for long.**
  An SD card doing internal wear-levelling can stall a single write for 100–250 ms.
  That must never happen on `ctrl`, or the gimbal freezes mid-track.
- **Config is guarded by a mutex.** `ctrl` takes a local copy at the top of each
  step and never holds the mutex across the PID.
- **E-stop is ISR → `vTaskNotifyGiveFromISR` → `safety`.** No work in the ISR.
- **Static allocation** (`xQueueCreateStatic`, `xTaskCreateStatic`), plus
  `vApplicationMallocFailedHook` and `vApplicationStackOverflowHook`. No `malloc`
  after init, never in an ISR.
- The architecture document names **which variables cross task boundaries and what
  protects each one**.

### State machine

States: `BOOT → SELFTEST → ZONE_TOUR → DISARMED → ARMED → …`, with
`PARKED` (idle), `LINK_LOST` (selected channel silent) and a latched `FAULT` that
requires operator acknowledgement. The laser is forced off in `FAULT`, `PARKED`,
`DISARMED` and `LINK_LOST`.

**`PARKED`, not deep sleep, is `AIM`'s idle mode**: servos detached, laser off,
display dimmed, WiFi modem-sleep. The ESP32-S3 cannot deep sleep while holding servo
position with PWM. Real deep sleep is `PILOT`'s job.

---

## 3. Communications

| Link | Interface | Role | Why | On failure |
|------|-----------|------|-----|------------|
| `EYE` (PC) ⟷ `AIM` (ESP32-S3), control | **UART1**, 115200 8N1 | duplex | Lowest latency; dead time sets the gain ceiling | 300 ms without a valid frame → `LINK_LOST`, axes stop, PIDs reset |
| `EYE` ⟷ `AIM`, config + telemetry | **MQTT** over WiFi | duplex | Config plane and telemetry; broker on `EYE` | Backoff reconnect; MQTT Last Will announces the drop |
| `PILOT` (ESP32-C3) ⟷ `AIM` *(Phase 1)* | **ESP-NOW** | duplex | Both ends are Espressif; no pairing, no broker, coexists with WiFi | Staleness timeout → `LINK_LOST` if it is the selected channel |
| `AIM` ⟷ SD card | **SPI** master | write | Phase 0 storage | Four named conditions, §5 |
| `AIM` ⟷ OLED | **I²C** 400 kHz | write | Only device on the bus | Log once, disable `ui`, **keep controlling** |
| `AIM` ⟷ `VAULT` (STM32) *(Phase 1)* | **SPI**, `AIM` master | write + status | Fixed-size opaque records, DMA | Sequence + CRC, error counters, resync on framing loss |

**UART0 is console only.** The data link moved to UART1 on spare GPIOs via a
USB-TTL adapter, because sharing the port with log output is a known hazard —
log text lands in the data stream, and the fire command has to be hardened so that
no line merely *starting* with `F` can trigger a shot.

### Wire formats

Split by traffic class:

| Class | Path | Format |
|-------|------|--------|
| Control (error vector, at frame rate) | UART1 | compact ASCII — `E <dx> <dy> <valid>` |
| Config, commands, telemetry | UART1 + MQTT | **NDJSON**, one object per line |

The framing contract, which is required in writing:

- **Maximum line length 256 bytes.** Overflow → discard to the next newline and
  **count it**. Never grow the buffer, never truncate-and-parse.
- Every numeric field range-checked before use. `NaN` and `inf` rejected explicitly
  — a `NaN` error reaching the PID poisons the integrator permanently.
- **CRC-8 on NDJSON lines.** Mismatches counted, not fatal.
- Counters (`bad_crc`, `overlong`, `unparsed`, `out_of_range`) exposed in telemetry
  and on the OLED.

---

## 4. Input channels

**Exactly one channel is processed at a time.** Selection is a configuration value,
normally set by an MQTT configuration message.

| `input.channel` | Source | Accepted when |
|---|---|---|
| `AUTO` | Error vector from `EYE`'s (PC) vision pipeline | `ARMED`, source fresh |
| `MANUAL` | Mouse-driven velocity from `EYE`'s manual script | `ARMED`, source fresh |
| `PILOT` *(Phase 1)* | ESP-NOW frames from `PILOT` (ESP32-C3) | `ARMED`, source fresh |
| `NONE` | — | Motion commands ignored entirely |

- Selection is **validated, applied, persisted to NVS and acknowledged** on the
  config state topic. A silently ignored mode change is indistinguishable from a
  broken one.
- **Non-selected channels are still received and counted**, then dropped before the
  controller, with a per-channel `dropped_inactive` counter. Exclusivity is
  observable rather than invisible.
- **Switching resets both PIDs and zeroes the commanded velocity**, so the gimbal
  does not jump on handover.
- The selected channel going stale → `LINK_LOST`. **No silent fallback** to another
  channel; exclusivity is the point.

### Emergency stop is not a channel

E-stop is a distinct message accepted from **any** transport, in **any** state,
regardless of `input.channel`. It is handled by the realtime `safety` task and
latches `FAULT`; recovery needs an explicit operator acknowledgement. The local
button on the `AIM` board does the same over a wire and works with no radio at all.

Routing E-stop through channel selection would mean that selecting `AUTO`
disables `PILOT`'s emergency stop — a safety defect, not a design preference.

---

## 5. Configuration and storage

### Configuration plane

One versioned, flat key space rather than ad-hoc settings:

```text
input.channel                    pid.pan.{kp,ki,kd}      pid.tilt.{kp,ki,kd}
zone.{pan,tilt}.{min,max}        laser.brightness        telemetry.rate_hz
log.sd.enabled                   telemetry.wifi.enabled  telemetry.ble.enabled
```

- **Precedence:** compiled defaults → NVS → runtime message (MQTT `.../config/set`,
  or NDJSON over UART1).
- Every write validated, applied, persisted and **acknowledged** on `.../config/state`;
  rejections carry a reason.
- A **schema version** in NVS, so a stale blob is rejected rather than misread.
- `factory reset` command.
- **Defaults are the safe ones:** transmission off, logging on, `input.channel = NONE`,
  laser off.

**Storage and transmission are independent switches.** Turning WiFi telemetry off
does not stop recording to the card — otherwise a demo with WiFi disabled silently
stops producing the evidence the soak test depends on.

### SD card logging

Rolling files with overwrite of the oldest when full.

**Layout.** `N` pre-created fixed-size segments `LOG0000.CSV … LOG00NN.CSV`, plus
`INDEX.TXT` holding the current segment, write offset and a monotonic sequence
number. Segments are **overwritten in place** (`f_lseek` + `f_write`) rather than
deleted and recreated — that avoids FAT churn and the power-loss window that comes
with it. Each segment header carries its sequence number so a reader can order
segments after a wrap.

**Write path.** `ctrl` and the link tasks push records to `log_q`; `logger` drains,
batches into a 4–8 KB buffer aligned to the card's block size, and writes whole
blocks. `f_sync` on segment boundaries and every N seconds — never per record.

**Back-pressure.** `log_q` is deep and the policy is **drop-oldest with a counter**,
never block. Telemetry is lossy by design; the control loop is not.

**Record format.** CSV — one line per control step or event, fixed columns:

```text
seq, t_mono_us, t_wall_iso, state, channel, ex, ey, vpan, vtilt, pan, tilt, flags
```

CSV because the card should be directly useful on a PC, which is why SD was chosen
over raw NOR flash.

**Timestamps** are owned by `AIM`. Monotonic microseconds **always**; wall clock
**when available**. Recording both means a log taken with no network is still
ordered and still useful. The wall-clock source — SNTP, an on-board RTC, or set by
`EYE` over the link — is still open; monotonic time works meanwhile.

**Failure handling.** Four named, tested conditions — *no card*, *card removed while
running*, *card full*, *write error*. Each logs once, raises an OLED status flag,
and **never stops the control loop**. Mount is retried on a slow timer, so a card
inserted mid-run starts working.

### Storage telemetry

Card health and write performance are part of the telemetry `AIM` publishes over
the active channels — not just internal state:

```text
sd.present      sd.mounted            sd.full            sd.free_bytes
sd.segment      sd.write_errors       sd.dropped_records sd.queue_depth
sd.write_bytes_per_s   sd.write_max_latency_us   sd.write_p95_latency_us
```

Two things this buys beyond visibility:

- **`sd.write_max_latency_us` is the early warning for the stall problem.** A card
  doing internal wear-levelling that blocks 200 ms shows up here as a number, rather
  than being discovered as a frozen gimbal.
- **Continuous critical-section timing.** These are live measurements of a real
  critical path, which is stronger evidence than a one-off figure in a document.

**DMA.** With `PILOT` in Phase 1, the SD path is the only DMA candidate left in
Phase 0. ESP-IDF's `sdspi` driver takes a DMA channel when the SPI bus is
initialised (`spi_bus_initialize(..., SPI_DMA_CH_AUTO)` in the IDF `sd_card/sdspi`
example) — **confirm this against the IDF version in use**, because requirement 6.2
now rests on it. If it does not hold, `PILOT`'s `adc_continuous` covers 6.2 when it
lands in Phase 1.

---

## 6. Control

The strongest part of the project, and preserved deliberately through the FreeRTOS
port.

- **The plant is an integrator.** Servo velocity → angle → dot position is `P(s) = k/s`.
  Commanding velocity rather than position is what makes it that.
- **Dead time is the binding constraint.** Camera → vision → link is 50–250 ms, and
  that, not the mechanics, sets the gain ceiling.
- **Feedback is real.** The measurement comes from the camera and is independent of
  what was commanded, which is what lets the loop reject gravity droop, servo
  deadband, backlash and a horn that slipped on its spline.

### Bounds and saturation

- Travel clamped to mechanical limits ∩ the working zone. The zone is deliberately
  much smaller than full travel: a bad gain or a confused detector should run the
  dot into a soft edge inside the scene, not sweep it across the room.
- **Rate clamped at two levels** — a hard ceiling in `Gimbal::setVelocity()` that no
  input source can get around, and the PID's own output clamp, which is what the
  anti-windup logic treats as saturated. A `static_assert` enforces that the PID
  clamp stays at or below the hardware ceiling; if it were higher the PID would
  believe it was in range while the gimbal quietly limited the rate, and
  conditional integration would wind up against a limit it cannot observe.
- Anti-windup by conditional integration; filtered derivative.

### Tuning

Gains are settable at runtime over the link (`K b 40 4 0`, `N 8 0` nudge, `T 1`
telemetry, `Q` query) and now **persist in NVS** rather than reverting on reboot.
The `N` nudge is a repeatable, known open-loop disturbance the controller is not
told about — the only way to compare two gain sets meaningfully.

### Boot zone tour

At startup the lit laser walks the perimeter of the working zone clockwise from the
top-left, then parks in the centre. **The direction is the test:** a
counter-clockwise trace means an axis-geometry flag is wrong, and that same wrong
flag is what would send the tracking loop running *away* from the target. Reading
it off a 4-second trace is much cheaper than discovering it as a runaway.

---

## 7. Safety

Split across phases: the **firmware** half is Phase 0, the **hardware** half Phase 1.

A relay-driven laser has a power-on defect that lights the beam without being asked:
the relay GPIO floats from power-on until `Relay::init()` runs and the active-low
module reads it as ON; and `gpio_config()` enables the output before `off()` writes
a level, so the pin is briefly driven into the energised state.

**Phase 0 closes the firmware half** — setting the pin level before configuring it
as an output, and an internal pull-up so the pre-`init()` window rests off. About
five lines, and it removes the code-side path entirely.

**Phase 1 closes the hardware half.** An external pull-up on the driver input is the
only fix that covers the window from power-on through the bootloader, since nothing
in firmware is running yet. Replacing the relay with a MOSFET low-side switch is
worth doing in the same pass: a mechanical relay cannot be dimmed, so the MOSFET both
cures the glitch and enables PWM brightness — which lets the beam run at the lowest
level the camera can still see. That is safer *and* it fixes the detection
blooming that a beam stuck at full brightness causes.

The interlock, in both phases: the beam may be lit only when **all** of — state is `ARMED`,
link fresh, WDT healthy, no E-stop latch, beam requested — hold. One
`bool laserPermitted()` in the `safety` task, with the reason for any denial logged.
