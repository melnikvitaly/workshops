# Final Project — Laser Gimbal with Camera Tracking

A laser on a 2-axis gimbal that closes its control loop **through a camera**. A PC
watches both the laser dot and the target, computes the error vector between them,
and streams it to an ESP32-S3. The firmware runs one PID per axis and drives the
servos **by velocity**, not position. This is image-based visual servoing — the same
structure a camera gimbal tracker uses.

The gimbal can also be driven by hand, from the PC or from a wireless joystick, and
every control step is logged to an SD card with timestamps.

```text
  camera ──►    EYE     ──► error vector ──UART1──►    AIM    ──► SD card
                 ▲                                     │
                 │                                 PID per axis
                 │                                     │
                 │                               velocity (deg/s)
                 │                                     │
                 │                             Gimbal integrates ──► servos
                 │                                     │
                 └────────── laser dot moves ◄─────────┘

  PILOT ──ESP-NOW──► AIM          EYE ──MQTT──► AIM          AIM ──SPI──► VAULT
   joystick, E-stop                config, telemetry              (Phase 1)
```

**EYE** (PC) sees · **AIM** (ESP32-S3) decides and acts · **PILOT** (ESP32-C3) is
the human hand · **VAULT** (STM32) remembers. Roles are named in §2 — the role is
what the design commits to, the chip in brackets is how it happens to be hosted.

> **Plan status.** Revision 3, matching the decisions in [`REVIEW.md`](./REVIEW.md).
> Every review finding is resolved except **R-13** (reliability). Phase 0 currently
> scores **33.5 / 42 = 79.8%** against
> [`VERIFICATION/Requirements.html`](./VERIFICATION/Requirements.html) — see
> `REVIEW.md` §0 for the three levers that clear the 80% bar.

---

## 1. Phases

The project is built in three phases with a hard cut line. **Phase 0 is the graded
project**; nothing from Phase 1 opens until Phase 0 demonstrates end-to-end.

| Phase | Nodes | Contents |
|-------|-------|----------|
| **0 — graded** | EYE (PC), AIM (ESP32-S3), PILOT (ESP32-C3) | Tracking loop on FreeRTOS, FSM, SD logging with health telemetry, UART1 + MQTT to `EYE`, ESP-NOW from `PILOT`, performance instrumentation, documentation, **schematics for both boards** |
| **1 — after Phase 0 demos** | + VAULT (STM32) | **PCB layouts for both boards**, MOSFET laser driver, `VAULT` storage node, SD DMA, fault-injection console, BLE telemetry, CSRT |
| **2 — cut without regret** | — | NRF24L01+, on-camera YOLO, target-velocity feed-forward, joystick OLED, PWM laser brightness |

> **Open recommendation — `REVIEW.md` R-26.** Four of block 7's seven items (7.2,
> 7.4, 7.5, 7.6) can only be evidenced by a **layout**, so schematics alone score
> 3/7 and leave Phase 0 at 79.8% — just under the bar. Since no board is being
> fabricated, a layout costs drawing time only, and `workshop-5-1` is already most
> of the `AIM` board. Routing the **`AIM` board alone** in Phase 0 is worth +3 items.

**`VAULT` is not a separate node in Phase 0** — `AIM` plays that role itself,
writing telemetry straight to the SD card over SPI. When the STM32 arrives in
Phase 1 it takes the role over and the record format moves across unchanged.

---

## 2. Nodes

Each node has a **role name** that does not mention its silicon. The names are the
namespace used throughout the project — log tags, MQTT topics, source directories —
so a node can be re-hosted on different hardware without a rename cascade.

| Name | Role | Runs on | Phase |
|------|------|---------|-------|
| **EYE** | Sees. Detection, error vector, operator console, broker | PC | 0 |
| **AIM** | Decides and acts. Control loop, laser, storage, gateway | ESP32-S3 | 0 |
| **PILOT** | Human input. Joystick, emergency stop | ESP32-C3 | 0 |
| **VAULT** | Remembers. Storage medium only | STM32 | 1 |

The names are also the reason the STM32 could be deferred without a redesign:
`VAULT` is a role, and in Phase 0 that role is played by `AIM` itself.

```text
log tag   chip        MQTT topic                          source directory
EYE       PC          lasergimbal/eye/…                   eye/
AIM       ESP32-S3    lasergimbal/aim/{telemetry,config}  firmware/aim/
PILOT     ESP32-C3    lasergimbal/pilot/…                 firmware/pilot/
VAULT     STM32       lasergimbal/vault/…                 firmware/vault/
```

### EYE — vision and operator console *(PC)*

- Detects the red laser dot and the black target dot, computes the error vector.
- Detection methods are encapsulated behind one interface:
  - **method 1 (Phase 0)** — OpenCV threshold + shape gate, as in `workshop-5-miniproject`
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
- Accepts commands from exactly one input channel at a time (§5).

### PILOT — wireless remote *(ESP32-C3)*

- 2-axis analog joystick controls gimbal velocity.
- **ESP-NOW** to `AIM` (primary). BLE and NRF24 are alternatives behind the same
  transport interface, Phase 1/2.
- Battery powered with real **deep sleep**: auto-sleep after inactivity, wake on the
  joystick button GPIO, counters preserved in RTC memory.
- Carries the **emergency stop** button.

### VAULT — storage node *(STM32; Phase 1)*

Deliberately the thinnest node in the system: it owns the medium and nothing else.

- SPI **slave** to `AIM` (`AIM` is master), DMA, double-buffered.
- **No parsing, no timestamps, no state.** Records arrive fully formed and are
  written verbatim.
- SPI **master** to the SD card; FatFs and the rotation scheme move here from `AIM`.
- Reports card present / free space / write errors / current segment back to `AIM`.
- `IWDG`, refreshed only while the card and the link are both healthy.

---

## 3. Architecture

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

## 4. Communications

| Link | Interface | Role | Why | On failure |
|------|-----------|------|-----|------------|
| `EYE` (PC) ⟷ `AIM` (ESP32-S3), control | **UART1**, 115200 8N1 | duplex | Lowest latency; dead time sets the gain ceiling | 300 ms without a valid frame → `LINK_LOST`, axes stop, PIDs reset |
| `EYE` ⟷ `AIM`, config + telemetry | **MQTT** over WiFi | duplex | Config plane and telemetry; broker on `EYE` | Backoff reconnect; MQTT Last Will announces the drop |
| `PILOT` (ESP32-C3) ⟷ `AIM` | **ESP-NOW** | duplex | Both ends are Espressif; no pairing, no broker, coexists with WiFi | Staleness timeout → `LINK_LOST` if it is the selected channel |
| `AIM` ⟷ SD card | **SPI** master | write | Phase 0 storage | Four named conditions, §6 |
| `AIM` ⟷ OLED | **I²C** 400 kHz | write | Only device on the bus | Log once, disable `ui`, **keep controlling** |
| `AIM` ⟷ `VAULT` (STM32) *(Phase 1)* | **SPI**, `AIM` master | write + status | Fixed-size opaque records, DMA | Sequence + CRC, error counters, resync on framing loss |

**UART0 is console only.** The data link moved to UART1 on spare GPIOs via a
USB-TTL adapter, because sharing the port with log output is a known hazard in the
base project — log text lands in the data stream, and the fire command had to be
hardened so no line merely *starting* with `F` could trigger a shot.

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

## 5. Input channels

**Exactly one channel is processed at a time.** Selection is a configuration value,
normally set by an MQTT configuration message.

| `input.channel` | Source | Accepted when |
|---|---|---|
| `AUTO` | Error vector from `EYE`'s (PC) vision pipeline | `ARMED`, source fresh |
| `MANUAL` | Mouse-driven velocity from `EYE`'s manual script | `ARMED`, source fresh |
| `PILOT` | ESP-NOW frames from `PILOT` (ESP32-C3) | `ARMED`, source fresh |
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

## 6. Configuration and storage

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
ordered and still useful. ⏳ The wall-clock source is open — see `REVIEW.md` Q-8.

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

---

## 7. Control

Unchanged from `workshop-5-miniproject`, and the strongest part of the project —
preserved deliberately through the FreeRTOS port.

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

## 8. Safety

Split across phases: the **firmware** half is Phase 0, the **hardware** half Phase 1.

The base project has an unresolved defect where the beam lights without being asked:
the relay GPIO floats from power-on until `Relay::init()` runs and the active-low
module reads it as ON; and `gpio_config()` enables the output before `off()` writes
a level, so the pin is briefly driven into the energised state.

**Phase 0 — firmware, about five lines:**

1. `gpio_set_level()` to the inactive level **before** `gpio_config()`, so the pin
   is never briefly driven into the energised state during `init()`.
2. Internal pull-up configured, so the floating window before `init()` rests off.
3. The interlock below, in the `safety` task.

**Phase 1 — hardware:**

4. **External pull-up on the driver input** — the only fix covering the window from
   power-on through the bootloader.
5. **Replace the relay with a MOSFET low-side switch** on the `AIM` board. A mechanical
   relay cannot be dimmed; the MOSFET both cures the glitch and enables PWM
   brightness, letting the laser run at the lowest level the camera can still see.
   That is safer *and* it fixes the detection blooming problem.
6. Verify with a scope on the gate across power-on, reset and flash.

Then the interlock: the beam may be lit only when **all** of — state is `ARMED`,
link fresh, WDT healthy, no E-stop latch, beam requested — hold. One
`bool laserPermitted()` in the `safety` task, with the reason for any denial logged.

---

## 9. Hardware and PCB

Schematic first, then layout, for **two boards**.

### `AIM` board — laser gimbal controller (ESP32-S3) — *the graded board*

ESP32-S3-WROOM-1; USB-C input, BQ24040 Li-Ion charger and TLV758P LDO carried over
from `workshop-5-1`; servo power rail; **MOSFET laser driver**; micro-SD on SPI;
OLED I²C header; UART1 header; E-stop and control buttons.

| Requirement | How this board answers it |
|---|---|
| Power filtering | Bulk electrolytic on the servo rail sized for stall current, 10 µF + 100 nF per rail, 100 nF at every IC pin, ferrite between servo rail and logic, RC on analog inputs |
| Power/logic separation | Servos are a noisy inductive 5–6 V load with amp-level stall transients; the 3.3 V logic and the SD card are not. Separate pours, single-point star ground, servo return never shared with SD or ADC ground |
| High-speed routing | USB D± as a 90 Ω differential pair, length-matched, no stubs or vias on the pair; SD SPI kept short with a continuous return path directly beneath it; RF keep-out under the module |
| Test points | 3V3, VSERVO, VBAT, GND ×2, laser gate, SD SCK/MOSI/MISO/CS, UART1 TX/RX — labelled |

### `PILOT` board — wireless remote (ESP32-C3)

ESP32-C3-MINI-1, USB-C + charger + LDO, 2-axis joystick with RC filtering, E-stop
button, optional OLED header, optional nRF24 header (Phase 2), battery connector,
test points.

### Per-board deliverables

Mirroring `workshop-5-1`: DRC clean with no unwaived errors, front/back copper
plots, 3D render, BOM, and a `DESIGN-REVIEW.md` explaining the handful of decisions
actually made. That last document is what the PCB part of the demo is built on.

> **Note.** The verification block is satisfied by *one* board done properly. The
> `PILOT` board is worth doing only once the `AIM` board is finished.

---

## 10. Repository layout

The final project is **fully autonomous**. Sources are **copied** from the base
projects, never referenced across directories; nothing in the build reaches outside
this folder.

```text
workshop-7-final_project/
  README.md              this plan
  REVIEW.md              review, decisions log, open findings
  PROVENANCE.md          what was copied from where, at which commit
  firmware/
    aim/                 gimbal controller — ESP32-S3 (ESP-IDF / PlatformIO)
      src/
        drivers/         Uart, Pwm, Sdcard, Ssd1306, LaserDriver
        parts/           Gimbal, Laser, StatusLed
        tasks/           ctrl, safety, link_uart, link_net, radio, logger, ui
        transport/       ITransport + Uart / Mqtt / EspNow implementations
        utils/           Pid, RingQueue, Crc8
        Config.hpp  ConfigStore.hpp  StateMachine.hpp  Pinout.hpp
    pilot/               wireless remote — ESP32-C3
    vault/               storage controller — STM32 (Phase 1)
  eye/                   vision host — PC (Python)
    camera/              detection, overlay, controls, serial link
    tools/               fuzz_link.py, log plotting, soak analysis
  hardware/
    aim-board/           ESP32-S3 controller — KiCad, DRC, plots, BOM, DESIGN-REVIEW.md
    pilot-board/         ESP32-C3 remote — KiCad, DRC, plots, BOM, DESIGN-REVIEW.md
  mqtt/config/           mosquitto.conf, credentials (not committed)
  docs/                  architecture, interfaces, protocol, bringup
  VERIFICATION/          requirements sheet + coverage tracking
```

---

## 11. Still to be decided

Tracked in [`REVIEW.md`](./REVIEW.md). Phase 0 is at 79.8%; any one of these clears
the bar.

| Area | Finding | Worth | Blocks |
|------|---------|-------|--------|
| **Reliability: WDT, PC-side reconnect, input fuzzing** — the only unanswered finding | **R-13** | **+2.5** | 5.1, 5.2, 5.4, 9.1 |
| Route the `AIM` board in Phase 0 rather than deferring both layouts | **R-26** | **+3.0** | 7.2, 7.4, 7.5, 7.6 |
| A nine-shot list for the demo video, incl. one unbroken 5-minute take | **R-25** | **+2.5** | 9.1–9.4 |

Open questions: wall-clock time source (Q-8), local override for channel selection
when the broker is down (Q-9), which PCB to protect if only one finishes (Q-10), log
retention (Q-11), and confirmation of the PCB phasing (Q-13) and that `PILOT`'s
firmware lands in Phase 0 (Q-14).

---

## 12. Demonstration video

The demo is a recorded video rather than a live run. Block 9 is scored on what the
video *contains*, so it is shot to a list. ⏳ Proposed — `REVIEW.md` R-25.

| # | Shot | Scores |
|---|------|--------|
| 1 | Power on → self-test → zone tour, direction check explained | 9.3 |
| 2 | Arm → tracking → move the target by hand | 9.1 |
| 3 | `N` nudge disturbance → recovery, telemetry graph visible | 9.2 |
| 4 | Channel switch via MQTT config → joystick takes over | 9.2, 9.3 |
| 5 | Unplug the data link → failsafe → reconnect | 9.2 |
| 6 | Pull the SD card mid-track → `sd.present` false, gimbal keeps tracking | 9.2 |
| 7 | Garbage on UART1 → error counters climb, uptime does not | 9.2 |
| 8 | **One unbroken 5-minute tracking take with the uptime counter on screen** | **9.1** |
| 9 | Screen recording: schematic walkthrough, task table, FSM diagram | 9.3, 9.4 |

Shot 8 is the one that is easy to miss: an edited highlight reel does not
demonstrate stability — a single continuous take does.

---

## Verification

- Requirements: [`VERIFICATION/Requirements.html`](./VERIFICATION/Requirements.html)
  ([source sheet](https://docs.google.com/spreadsheets/d/1Eip9bvWQhd6_9XutWGIf_u8ECNoNm-KQcv1kDBZhbcY/edit?gid=0#gid=0))
- Coverage analysis and score tracking: [`REVIEW.md`](./REVIEW.md) §5
- Presentation ([template](https://docs.google.com/presentation/d/1kyIhydXwOALcR5MxjU38RSwIHLpqRsR5ne6F9L6wNbw/edit?slide=id.p1#slide=id.p1))
- Video demonstration
