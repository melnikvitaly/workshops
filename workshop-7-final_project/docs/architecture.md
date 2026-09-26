# Architecture — Laser Gimbal with Camera Tracking

The technical design of the system: what each node is responsible for, how `AIM`'s
tasks are structured, how the nodes talk, and the contracts that the control,
storage and safety paths hold to.

The project plan — phases, hardware deliverables, repository layout and the demo —
is in [`../README.md`](../README.md).

**Contents**

1. [Nodes](#1-nodes)
2. [Task architecture — `AIM`](#2-task-architecture--aim)
3. [Communications](#3-communications)
4. [Input channels](#4-input-channels)
5. [Configuration and storage](#5-configuration-and-storage)
6. [Control](#6-control)
7. [Safety](#7-safety)
8. [Performance instrumentation](#8-performance-instrumentation)

---

## 1. Nodes

Each node has a **role name** that does not mention its silicon — the role is
what the design commits to, the chip is how it happens to be hosted. The names are
the namespace used throughout the project — log tags, source directories — so a
node can be re-hosted on different hardware without a rename cascade.

Phase 0 has two nodes.

**Schematic first, then layout.** Phase 0 draws and routes one board —
`AIM`, the graded board.

| Name | Role | Runs on | Board |
|------|------|---------|-------|
| **EYE** | Sees. Detection, error vector, operator console | PC | — (host PC) |
| **AIM** | Decides and acts. Control loop, laser, storage | ESP32-S3 | ESP32-S3-WROOM-1 DevKit; USB-C input; SG90 servo power rail; 1-channel relay laser driver; micro-SD on SPI; OLED I²C header; UART1 header; E-stop, `MODE` and control buttons (planned) |

```text
log tag   chip        source directory
EYE       PC          eye/
AIM       ESP32-S3    firmware/aim/
```

### `AIM` board requirements — *the graded board*

| Requirement | How this board answers it |
|---|---|
| Power filtering | Bulk electrolytic on the servo rail sized for stall current, 10 µF + 100 nF per rail, 100 nF at every IC pin, ferrite between servo rail and logic, RC on analog inputs |
| Power/logic separation | Servos are a noisy inductive 5–6 V load with amp-level stall transients; the 3.3 V logic and the SD card are not. Separate pours, single-point star ground, servo return never shared with SD or ADC ground |
| High-speed routing | USB D± as a 90 Ω differential pair, length-matched, no stubs or vias on the pair; SD SPI kept short with a continuous return path directly beneath it; RF keep-out under the module |
| Test points | 3V3, VSERVO, VBAT, GND ×2, laser gate, SD SCK/MOSI/MISO/CS, UART1 TX/RX — labelled |

### Node responsibilities

#### EYE — vision and operator console *(PC)*

- Detects the red laser dot and the black target dot, computes the error vector.
- Detection is encapsulated behind one interface: OpenCV threshold + shape gate.
- Streams the error vector to `AIM` over **UART1** — the only link.
- Hosts the operator UI: gain presets, nudge, telemetry graph, fire button, and the
  NDJSON configuration lines that select the input channel.
- A separate script drives the gimbal manually from the mouse.

#### AIM — gimbal controller *(ESP32-S3; the core of the project)*

- Runs the two PIDs and drives the servos by velocity.
- Owns the **SD card** over SPI, and owns **timestamps** for every record.
- Renders state on a 0.96" I²C OLED: error, gains, aiming status, active input
  channel, link and storage health, CPU load.
- Telemetry rides the UART1 NDJSON channel.
- Accepts commands from exactly one input channel at a time (§4).

---

## 2. Task architecture — `AIM`

### FreeRTOS task set — `AIM` (ESP32-S3)

| Task | Core | Prio | Period | Owns | Talks via |
|------|------|------|--------|------|-----------|
| `ctrl` | 1 | high | 20 ms, `vTaskDelayUntil` | PIDs, gimbal, servo PWM | reads `cmd_q`, writes `log_q` |
| `safety` | 1 | realtime | bounded wait (250 ms), notify or timeout | E-stop, laser interlock, TWDT subscriber | ISR → task notification |
| `link_uart` | 0 | normal | event | UART1 RX/TX, framing | writes `cmd_q` |
| `logger` | 0 | **low** | drains `log_q` | SD card, FatFs | reads `log_q` |
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
- **`ctrl` and `safety` subscribe to the task watchdog (TWDT), ~1 s, panic on
  timeout.** `ctrl` feeds it every 20 ms step; `safety` no longer blocks
  `portMAX_DELAY` on the E-stop notify — it waits in 250 ms slices so it can
  feed the TWDT between notifications. See §7.
- **Static allocation** (`xQueueCreateStatic`, `xTaskCreateStatic`), plus
  `vApplicationMallocFailedHook` and `vApplicationStackOverflowHook`. No `malloc`
  after init, never in an ISR.
- The architecture document names **which variables cross task boundaries and what
  protects each one**.

### What crosses a task boundary, and what protects it

| Variable | Producers → consumers | Protection |
|---|---|---|
| `cmd_q` | `link_uart`, `ui` → `ctrl` | static FreeRTOS queue (tagged-union item) |
| `log_q` | `ctrl`, `link_uart` → `logger` | static FreeRTOS queue, drop-oldest with a counter |
| `ConfigStore` blob | `link_uart`, `ui` write → `ctrl` snapshot | static mutex; `ctrl` copies the whole struct out and releases before the PID |
| `Fsm::state` | `ctrl` writes → `ui`, `link_uart`, `safety` read | `std::atomic<State>` |
| `estopLatched` | `safety` writes → `ctrl`, `laserPermitted()` read | `std::atomic<bool>` |
| `estopSource` | ISR / `link_uart` write → `ctrl` read | `std::atomic<EstopSource>` |
| `linkFresh` | `ctrl` writes → `safety` / `laserPermitted()` read | `std::atomic<bool>` |
| receiver counters (`bad_crc`, `unparsed`, `oor`, `drop_inact`, `logDropped`) | `link_uart`, `ctrl` write → `ui`, `link_uart` read | `std::atomic<uint32_t>` |
| E-stop signal | E-stop ISR / `link_uart` → `safety` task | task notification (`vTaskNotifyGiveFromISR` / `xTaskNotifyGive`) |
| `TelemSample` (latest control sample) | `ctrl` writes → `link_uart` reads | none — single writer, lossy reader, telemetry only |
| `Sd` (SD health + write stats) | `logger` writes → `link_uart`, `ui` read | none — single writer, lossy readers, telemetry only |
| `safetyTaskHandle` | `app_main` sets once, before the ISR is installed | publish-before-use ordering |

The config plane runs **one path** — `ConfigStore::set()` — for every writer. An
NDJSON `cfg.set` and the local `MODE` button both call it (validate → apply →
persist to NVS → the caller emits `cfg.state`); the NVS commit happens in the
writer's task (`link_uart` or `ui`), never on `ctrl`. `ctrl` notices a changed
`input.channel` in its next snapshot and runs the handover reset — both PIDs
reset, commanded velocity zeroed — so there is one reset path regardless of which
writer changed the channel.

### State machine

States: `BOOT → SELFTEST → DISARMED → ARMED → …` (with `boot.tour` on, a
`ZONE_TOUR` runs between `SELFTEST` and `DISARMED`), plus
`PARKED` (idle), `LINK_LOST` (selected channel silent) and a latched `FAULT` that
requires operator acknowledgement. The laser is forced off in `BOOT`, `SELFTEST`,
`DISARMED`, `LINK_LOST`, `PARKED` and `FAULT`; it is permitted only in `ZONE_TOUR`
(lit for the boot geometry check, no link required) and `ARMED` (lit only while
the selected channel is fresh). `laserPermitted()` in `safety` is the single gate.

Every transition is logged with its trigger — an `evt` line on the link, an
`ESP_LOGI`, and a `log_q` record. Triggers are short stable tokens: `boot`,
`selftest.ok`, `tour.done`, `btn.control`, `link.stale`, `link.fresh`, `estop`,
`fault.ack`, `idle`, `cfg.channel`. `FAULT` is latched; only `fault.ack` (the
`CONTROL` button or `{"t":"cfg.set","k":"fault.ack","v":true}`) leaves it, to
`DISARMED`.

**`PARKED`, not deep sleep, is `AIM`'s idle mode**: servos detached, laser off,
display dimmed. The ESP32-S3 cannot deep sleep while holding servo position with
PWM.

---

## 3. Communications

| Link | Interface | Role | Why | On failure |
|------|-----------|------|-----|------------|
| `EYE` (PC) ⟷ `AIM` (ESP32-S3), control | **UART1**, 115200 8N1 | duplex | Lowest latency; dead time sets the gain ceiling | 300 ms without any frame on the selected channel → `LINK_LOST`, axes stop, PIDs reset (`valid`/`targetVisible` does not matter — a steady stream of `valid=0` frames still counts as fresh) |
| `EYE` ⟷ `AIM`, config + telemetry | **UART1** NDJSON, same wire as the control path | duplex | One link, one failure mode, nothing to reconcile on reconnect | Same 300 ms staleness rule; config state re-sent on the next valid frame |
| `AIM` ⟷ SD card | **SPI** master | write | Phase 0 storage | Four named conditions, §5 |
| `AIM` ⟷ OLED | **I²C** 400 kHz | write | Only device on the bus | Log once, disable `ui`, **keep controlling** |

**UART0 is console only.** The data link moved to UART1 on spare GPIOs via a
USB-TTL adapter, because sharing the port with log output is a known hazard —
log text lands in the data stream, and the fire command has to be hardened so that
no line merely *starting* with `F` can trigger a shot.

### Wire formats

Two traffic classes share UART1: compact ASCII for the control path (frame
rate), NDJSON for everything else. The full grammar, the 256-byte line cap,
range checks and the CRC-8 are specified in
[`protocol.md`](./protocol.md) — this document is the wire contract's
source of truth.

---

## 4. Input channels

**Exactly one channel is processed at a time.** Selection is a configuration value,
normally set by an NDJSON configuration line from `EYE` over UART1.

| `input.channel` | Source | Accepted when |
|---|---|---|
| `AUTO_POSITIONAL` | Error vector from `EYE`'s (PC) vision pipeline | `ARMED`, source fresh |
| `AUTO_VELOCITYEQUATION` | Same error vector, driven by a velocity-equation PID instead of `AUTO_POSITIONAL`'s rate-output — see [`servo-control-strategies.md`](./servo-control-strategies.md) | `ARMED`, source fresh |
| `MANUAL` | Keyboard-driven velocity from `EYE`'s manual control | `ARMED`, source fresh |
| `NONE` | — | Motion commands ignored entirely |

- Selection is **validated, applied, persisted to NVS and acknowledged** on the
  config-state channel. A silently ignored mode change is indistinguishable from a
  broken one.
- **Non-selected channels are still received and counted**, then dropped before the
  controller, with a per-channel `dropped_inactive` counter. Exclusivity is
  observable rather than invisible.
- **Switching resets both PIDs and zeroes the commanded velocity**, so the gimbal
  does not jump on handover.
- The selected channel going stale → `LINK_LOST`. **No silent fallback** to another
  channel; exclusivity is the point.

### Local mode button

Selection normally arrives from `EYE` over the link, which means that with the link
down the node cannot be re-tasked at all — a single point of failure in the config
plane, not in the control loop. A **momentary `MODE` button on the `AIM` board**
removes it.

| Gesture | Effect |
|---|---|
| Short press | Advance to the next channel: `NONE → AUTO_POSITIONAL → MANUAL → AUTO_VELOCITYEQUATION → NONE` |
| Long press ≥ 1 s | Jump straight to `NONE` — cut all motion input without touching the E-stop latch |

- **The button does not write configuration.** It posts the same
  `config.set input.channel` item onto `cmd_q` that the NDJSON config path posts,
  so it runs through the one validate → apply → persist → acknowledge path and
  reuses the
  handover reset (both PIDs reset, commanded velocity zeroed). One writer, one code
  path, one set of counters — a second path that happened to skip the PID reset
  would be a jump on handover that only ever reproduces locally.
- **Owned by the `ui` task**, polled at 50 Hz with 30 ms debounce and acting on the
  release edge. It is deliberately **not** an interrupt: channel selection is not
  realtime, and the E-stop stays the only button on an ISR.
- **Selecting is not arming.** Motion still requires `ARMED` and a fresh source, and
  the handover reset zeroes velocity, so the button is safe to press in any state.
- **Precedence is last-writer-wins**, and NVS keeps whichever came last. `EYE` is
  told what the button did on the next valid frame.
- **Feedback has to be local**, since the case this exists for is the link being
  down: the OLED shows the new channel immediately, the status LED blinks its
  ordinal, and the change appears in the transition log and the SD `channel` column.
- `MODE` selects channels and nothing else. Arming and fault acknowledgement stay
  off it — overloading one button with safety-relevant actions is how a mode button
  becomes a hazard.

### Testing the working zone from `EYE`

`zone.{pan,tilt}.{min,max}` is regular config: set over `cfg.set` like any
other key, validated, persisted and applied live — no reboot needed. But the
zone is only *walked* automatically at boot if `boot.tour` is on (default
off). `control.zone_tour` is a `cfg.set` action key that re-enters
`ZONE_TOUR` on demand: set new bounds, send
`control.zone_tour`, and watch the beam trace the new rectangle rather than
guessing whether it covers the scene. It no-ops outside `DISARMED`/`PARKED`,
so it can never take over the gimbal from an operator mid-session — see
`docs/protocol.md` §3.3.

The widest `zone.*` can ever be set to is the mechanical travel itself
(`GIMBAL_PAN_MIN/MAX`, `GIMBAL_TILT_MIN/MAX` in `Config.hpp`), exposed
read-only as `zone.limit.{pan,tilt}.{min,max}` so `EYE` can read the true
ceiling with `cfg.get` instead of keeping its own copy of those numbers —
that is what the left panel's **Set Max Zone** button does.

### Emergency stop is not a channel

E-stop is a distinct message accepted from **any** transport, in **any** state,
regardless of `input.channel`. It is handled by the realtime `safety` task and
latches `FAULT`; recovery needs an explicit operator acknowledgement. The local
button on the `AIM` board does the same over a wire and works with no radio at all.

---

## 5. Configuration and storage

### Configuration plane

One versioned, flat key space rather than ad-hoc settings:

```text
input.channel                    pid.pan.{kp,ki,kd}          pid.tilt.{kp,ki,kd}
pid.veq.pan.{kp,ki,kd}           pid.veq.tilt.{kp,ki,kd}     (AUTO_VELOCITYEQUATION gains)
zone.{pan,tilt}.{min,max}        laser.brightness            telemetry.rate_hz
log.sd.enabled                   boot.tour
zone.limit.{pan,tilt}.{min,max}  (read-only - the mechanical travel, cfg.get only)
```

- **Precedence:** compiled defaults → NVS → runtime message (NDJSON over UART1).
- Every write validated, applied, persisted and **acknowledged** on the config-state
  channel; rejections carry a reason.
- A **schema version** in NVS, so a stale blob is rejected rather than misread.
- `factory reset` command.
- **Defaults are the safe ones:** transmission off, logging on, `input.channel = NONE`,
  laser off.

**Storage and transmission are independent switches.** Turning telemetry off
does not stop recording to the card — otherwise a demo with telemetry disabled
silently stops producing the evidence the soak test depends on.

### SD card logging

One append-only file.

**Layout.** A single append-only `LOG.CSV`, opened at boot and appended to for the
life of the run, with a **`BOOT` marker record** as its first line. When the card
fills, `sd.full` is raised and logging stops — the control loop does not. A card
outlasts any demo or soak run several times over, and *card full* is one of the
four failure conditions that has to be demonstrated anyway.

**Write path.** `ctrl` and the link tasks push records to `log_q`; `logger` drains,
batches into a 4–8 KB buffer aligned to the card's block size, and writes whole
blocks. `f_sync` every N seconds and on unmount — never per record.

**Back-pressure.** `log_q` is deep and the policy is **drop-oldest with a counter**,
never block. Telemetry is lossy by design; the control loop is not.

**Record format.** CSV — one line per control step or event, fixed columns:

```text
seq, t_mono_us, t_wall_iso, state, channel, ex, ey, vpan, vtilt, pan, tilt, flags
```

CSV because the card should be directly useful on a PC, which is why SD was chosen
over raw NOR flash.

**Timestamps** are owned by `AIM`. **Phase 0 has no wall clock at all** — records
carry monotonic microseconds since boot, and `t_wall_iso` is written **empty**. An
empty field is the honest encoding: a placeholder epoch such as `1970-01-01` looks
like a valid time to every reader that will ever open the file.

One thing replaces the clock, and it costs nothing:

- **The `BOOT` marker record.** Monotonic time restarts at zero on reset, so without
  a marker a reader cannot tell a reboot from a backwards jump in `seq`. The marker
  carries the reset reason and `t_mono_us = 0`, and `EYE` — which has a real clock —
  notes its arrival in its own log. One line per session converts an entire log to
  wall time offline, including alignment against the demo video.

**Failure handling.** Four named, tested conditions — *no card*, *card removed while
running*, *card full*, *write error*. Each logs once, raises an OLED status flag,
and **never stops the control loop**. Mount is retried on a slow timer, so a card
inserted mid-run starts working.

### Storage telemetry

Card health and write performance are part of the telemetry `AIM` publishes over
the active channels — not just internal state:

```text
sd.present      sd.mounted            sd.full            sd.free_bytes
sd.write_errors sd.dropped_records    sd.queue_depth     sd.sync_count
sd.write_bytes_per_s   sd.write_max_latency_us   sd.write_p95_latency_us
```

Two things this buys beyond visibility:

- **`sd.write_max_latency_us` is the early warning for the stall problem.** A card
  doing internal wear-levelling that blocks 200 ms shows up here as a number, rather
  than being discovered as a frozen gimbal.
- **Continuous critical-section timing.** These are live measurements of a real
  critical path, which is stronger evidence than a one-off figure in a document.

**DMA.** The SD path is the only DMA candidate in Phase 0. ESP-IDF's `sdspi`
driver takes a DMA channel when the SPI bus is initialised with
`spi_bus_initialize(..., SPI_DMA_CH_AUTO)`. **Confirmed on the version in use**
— ESP-IDF 6.0.1 (`framework-espidf` 4.60001.0): the bus gets an auto-assigned
channel and the `sdspi` device inherits it, so every block write is DMA-backed.
`Sdcard::mount()` logs the fact at boot. Requirement 6.2 rests on this.

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

The rate-output form above is what `AUTO_POSITIONAL` runs. The same camera
error is also available through a velocity-equation PID on the
`AUTO_VELOCITYEQUATION` channel, for comparing the two against each other on
the bench — see [`servo-control-strategies.md`](./servo-control-strategies.md)
for why rate-output is still the default.

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

### Zone tour

Set `boot.tour` (NVS, default off, changeable from `EYE` — "Tour on boot" or
`serial_link.py --boot-tour on`) and after `SELFTEST` the lit laser walks the
perimeter of the working zone clockwise from the
top-left, then parks in the centre. **The direction is the test:** a
counter-clockwise trace means an axis-geometry flag is wrong, and that same wrong
flag is what would send the tracking loop running *away* from the target. Reading
it off a 4-second trace is much cheaper than discovering it as a runaway.

---

## 7. Safety

A relay-driven laser has a power-on defect that lights the beam without being asked:
the relay GPIO floats from power-on until `Relay::init()` runs and the active-low
module reads it as ON; and `gpio_config()` enables the output before `off()` writes
a level, so the pin is briefly driven into the energised state.

**Phase 0 closes the firmware half** — setting the pin level before configuring it
as an output, and an internal pull-up so the pre-`init()` window rests off. About
five lines, and it removes the code-side path entirely.

The interlock: the beam may be lit only when **all** of — state is `ARMED` (or
`ZONE_TOUR`), link fresh, no E-stop latch — hold. One `bool laserPermitted()`
in `tasks/Safety.hpp`, read by `ctrl` and `ui`, with the reason for any denial
logged.

### Watchdog ⟦5.1⟧

`ctrl` and `safety` subscribe to `esp_task_wdt`, reconfigured at boot to a
~1 s timeout with `trigger_panic = true`. A "WDT healthy" term never needs a
place in `laserPermitted()`: a stalled task trips the watchdog and resets the
whole board, so there is no state where the loop is wedged but the laser
stays lit — the interlock only ever has to reason about a system that is
either running normally or rebooting.

- `main.cpp` logs `esp_reset_reason()` on every boot (`evt boot` line +
  `LOG.CSV` `BOOT` marker) and, on a reset caused by the TWDT or the
  interrupt/hardware WDT, increments a counter persisted in NVS
  (`ConfigStore::bumpWdtResetCount()`, key separate from the versioned config
  blob) and reports it as `wdt_resets` in the boot event.
- The same watchdog-reset branch forces `input.channel` back to `NONE`
  (through the normal `ConfigStore::set` path, so it persists) before `ctrl`
  ever reads its first config snapshot. The FSM already always boots into
  `SELFTEST` → `DISARMED`/`ZONE_TOUR` regardless of the reset reason, so this
  is what stops tracking from resuming unattended after a crash — an operator
  has to re-select a channel and re-arm.

---

## 8. Performance instrumentation

### The data path — one copy of the error at a time

Every hop the error vector makes between the camera sensor and the servo horn,
in order:

1. The OAK-1 sensor captures a frame; DepthAI moves it over USB into `tracker.py`
   on the PC.
2. `dots.py` runs the red-dot and black-dot detectors on that frame (host-side
   OpenCV) and computes `error = target_position − laser_dot_position`,
   normalised to `[-1, 1]`.
3. `serial_link.py` formats the error as the compact ASCII frame
   `E <dx> <dy> <valid>` and writes it to UART1.
4. `AIM`'s `link_uart` task reads the line, and `protocol::parse()` tokenises
   and range-checks it into a `protocol::Frame` (§3).
5. `handleAscii()` turns the `Frame` into a `CmdItem` and copies it onto
   `cmd_q`.
6. `ctrl` drains `cmd_q`; `AutoPositionalChannel::onErrorSample()` copies the
   vector into its own `_error` member, sign-corrected for the mounting
   (`PAN_INVERT`/`TILT_INVERT`).
7. `AutoPositionalChannel::update()` runs each axis's
   `PositionalPid::update(error, dt)`, turning the error into a commanded
   rate in deg/s.
8. `Gimbal::setVelocity()` clamps the rate to the hardware ceiling;
   `Gimbal::update()` integrates it into a target angle and calls
   `Servo::write(angle)`, clamped to the working zone.
   (On `AUTO_VELOCITYEQUATION`, steps 6-8 are
   `AutoVelocityEquationChannel::onErrorSample()` / `::update()` instead:
   `VelocityEquationPid::update()`'s output is a degrees delta added straight
   onto the current servo angle and passed to `Gimbal::moveTo()` — no rate,
   no outer integration step. See
   [`servo-control-strategies.md`](./servo-control-strategies.md).)
9. `Servo::write()` maps the angle linearly to a pulse width in microseconds
   and calls `PWM::writeMicroseconds()`, which converts that to an LEDC duty
   count and programs it with `ledc_set_duty()`/`ledc_update_duty()`.
10. The LEDC peripheral free-runs the 50 Hz waveform in hardware from that
    duty count alone — no further CPU involvement until the next update — and
    the servo horn moves. The next captured frame closes the loop.

Steps 4–5 are the `link_uart` span instrumented below as **parse**; steps
6–9, run inside one `ctrl` step, are the **pid** span — the same span the
`SCOPE` GPIO brackets, so the software timing and the logic-analyser trace
describe the identical piece of work.

### Timer and hardware-event configuration

Two hardware time sources are in use, and nothing else generates a periodic
interrupt in Phase 0:

- **LEDC** drives both servos: `LEDC_TIMER_0`, `LEDC_LOW_SPEED_MODE`, 50 Hz,
  `LEDC_TIMER_16_BIT` resolution, one channel per axis (`LEDC_CHANNEL_0`
  pan, `LEDC_CHANNEL_1` tilt) — full detail in
  [`interfaces.md` §5](./interfaces.md#5-ledc--servo-pwm). Once
  `ledc_set_duty`/`ledc_update_duty` programs a duty count, the peripheral
  free-runs the waveform in hardware; `ctrl` never touches it again until the
  next control step changes the angle.
- **`esp_timer`** is the one clock every timestamp in the firmware reads:
  every `t_mono_us`/`up` field on the wire and in `LOG.CSV`, and every span
  below, is `esp_timer_get_time()` — a free-running 64-bit microsecond
  counter, read on demand from ordinary task code. Nothing schedules an
  `esp_timer` callback or alarm anywhere in Phase 0; every use is a snapshot,
  never a timer ISR. `CONFIG_FREERTOS_RUN_TIME_STATS_USING_ESP_TIMER` wires
  FreeRTOS's own per-task runtime accounting (§8.2 below) to the same
  counter, so every "when" and "how long" figure the firmware produces
  shares one clock.

### 8.1 Step timing — `esp_timer_get_time()` spans

Three spans are timed with a scope-exit sampler (`ScopedPerf`,
`utils/PerfStat.hpp`) that tracks min, max and an EWMA (α = 0.2) in
microseconds, never resetting — the same "worst case ever, plus a live
trend" shape as the SD write-latency stats in §5:

| Span | Task | What it covers | `tlm.perf` key |
|---|---|---|---|
| `pid` | `ctrl` | One whole `step()` — queue drain, FSM dispatch, the PID, the gimbal integration | `pid` |
| `parse` | `link_uart` | One `handleLine()` call — NDJSON or ASCII, whichever the line is | `parse` |
| `render` | `ui` | One `Ui::render()` — building and flushing the OLED frame | `render` |

`link_uart` publishes all three at 1 Hz as `tlm.perf`, a message of its own
rather than folded into `tlm.sys` — the same reason `tlm`, `tlm.sd` and
`tlm.sys` are already split: one object carrying everything would not fit
the 256-byte line cap
([`protocol.md` §3.4](./protocol.md#34-telemetry-and-events)).

### 8.2 Per-task CPU% and stack high-water

`ui` — the one task `Ipc` collects every task handle into — calls
`uxTaskGetSystemState()` every `SYS_STATS_PERIOD_MS` (5 s), requiring
`CONFIG_FREERTOS_USE_TRACE_FACILITY` and
`CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS` (`sdkconfig.defaults`). For each of
the five tasks it computes that task's share of the runtime-counter delta
since the previous period — summed across both cores, so busy time across
every task (both idle tasks included) always totals to ~100% regardless of
core count — and reads `uxTaskGetStackHighWaterMark()` for the same five.

All five tasks' CPU% and stack high-water are logged locally with `ESP_LOGI`
every `SYS_STATS_PERIOD_MS` (5 s), per
[`coding.md`](./coding.md#memory). Only `ctrl`, `logger`,
`ui` and the idle remainder go out over the wire in `tlm.sys.cpu` — `safety`
and `link_uart` sit near 0% in normal operation and are left off the wire
message to keep its byte budget for the rest of it — alongside
`tlm.sys.stack_min`, the tightest of the five high-water marks. That is
deliberately the one number worth alarming on remotely: whichever task is
closest to a stack overflow, not which one.

### 8.3 The `SCOPE` probe

`GPIO47` (`pinout::SCOPE`) is driven high for the exact duration of `ctrl`'s
`step()` and low otherwise — nothing else touches it — so a logic analyser
sees one pulse per 20 ms control tick whose width is the real wall-clock cost
of the same span the `pid` entry in `tlm.perf` measures in software.

**Pending:** the code is in place
(`CtrlTask::initScopePin()` in
[`Ctrl.hpp`](../firmware/aim/src/tasks/Ctrl.hpp)), but no capture has been
taken yet — that needs the board on the bench with a logic analyser clipped
to `GPIO47`, which this pass did not have access to. The screenshot belongs
in [`../README.md`](../README.md) once it exists.
