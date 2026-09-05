# Task List

Working checklist for the final project. Plan: [`README.md`](./README.md) ·
Review and decisions: [`REVIEW.md`](./REVIEW.md).

Requirement IDs in `⟦…⟧` map to
[`VERIFICATION/Requirements.html`](./VERIFICATION/Requirements.html). Phase 0 as
listed scores **41 / 42 (97.6%)**; the bar is 34 / 42 (80%).

Nodes: **EYE** (PC) · **AIM** (ESP32-S3) · **PILOT** (ESP32-C3) · **VAULT** (STM32)

---

## Phase 0 — the graded project

Nodes: `EYE` + `AIM`. Ends on a fixed date (**still unset — decide this first**).

### 1. Autonomous project tree

- [ ] Create `firmware/aim/`, `eye/`, `hardware/`, `docs/` per the structure in `CLAUDE.md`
- [ ] Copy sources from `workshop-5-miniproject` — **copy, never reference**
- [ ] Build cleanly with no path reaching outside `workshop-7-final_project/`
- [ ] Commit the `final_project/` → `workshop-7-final_project/` move so history survives

### 2. FreeRTOS port, FSM, config plane ⟦1.1 1.3 1.5 2.4 3.1 3.2 3.3 3.4⟧

- [ ] Five tasks per `docs/architecture.md` §2: `ctrl`, `safety`, `link_uart`, `logger`, `ui` (`link_net` and `radio` are Phase 1)
- [ ] `cmd_q` — one queue, tagged-union item, many producers / one consumer ⟦3.2⟧
- [ ] Config mutex; `ctrl` takes a local copy, never holds it across the PID ⟦3.3⟧
- [ ] E-stop path: ISR → `vTaskNotifyGiveFromISR` → `safety` ⟦2.3⟧
- [ ] Static allocation; `vApplicationMallocFailedHook`, `vApplicationStackOverflowHook`
- [ ] No `malloc` after init, never in an ISR
- [ ] `StateMachine.hpp` — `BOOT → SELFTEST → ZONE_TOUR → DISARMED → ARMED`, plus `PARKED`, `LINK_LOST`, latched `FAULT` ⟦1.3⟧
- [ ] Laser forced off in `FAULT`, `PARKED`, `DISARMED`, `LINK_LOST`
- [ ] Every transition logged with its trigger
- [ ] Config plane: schema, NVS, precedence, validate → apply → persist → **acknowledge** ⟦4.3⟧
- [ ] Schema version in NVS; `factory reset`; safe defaults (transmission off, logging on, `input.channel = NONE`, laser off)
- [ ] Exclusive channel selector — `AUTO` / `MANUAL` / `NONE`; switching resets PIDs and zeroes velocity
- [ ] Non-selected channels received and counted (`dropped_inactive`), dropped before the controller
- [ ] **Local `MODE` button** — short press cycles `NONE → AUTO → MANUAL → NONE`; long press ≥ 1 s → `NONE` ⟦5.2⟧
- [ ] Button posts the same `config.set input.channel` item onto `cmd_q` as an NDJSON config line — one validate → apply → persist → acknowledge path, same handover reset
- [ ] `ui` owns it: 50 Hz poll, 30 ms debounce, release edge, **no ISR** (E-stop stays the only button interrupt)
- [ ] Local feedback with the link down: OLED channel name, LED blink ordinal, transition logged to SD
- [ ] **E-stop is not a channel** — accepted from any transport, any state ⟦5.4⟧

### 3. UART1 link and framing contract ⟦2.2 5.3⟧

- [ ] Move the data link to UART1 on spare GPIOs; UART0 stays console-only
- [ ] `ITransport` interface + `UartTransport` — the seam `MqttTransport` and `EspNowTransport` drop into in Phase 1 ⟦1.5⟧
- [ ] Compact ASCII on the control path; NDJSON for config, commands, telemetry
- [ ] 256-byte line cap → discard to next newline **and count it**, never grow ⟦5.3⟧
- [ ] Range-check every numeric field; reject `NaN` / `inf` explicitly ⟦4.4⟧
- [ ] CRC-8 on NDJSON lines; mismatches counted, not fatal
- [ ] Counters `bad_crc`, `overlong`, `unparsed`, `out_of_range` in telemetry and on the OLED
- [ ] Laser boot-safe GPIO — `gpio_set_level()` **before** `gpio_config()`, internal pull-up *(F-14a, ~5 lines)*
- [ ] `laserPermitted()` interlock in `safety`, denial reason logged

### 4. SD logging and health telemetry ⟦2.2 2.4 6.2⟧

- [ ] `esp_vfs_fat_sdspi_mount`, SPI master
- [ ] **Confirm the SPI bus gets a DMA channel at `spi_bus_initialize`** — requirement 6.2 rests on this now that `PILOT` is Phase 1 ⟦6.2⟧
- [ ] Single append-only `LOG.CSV`; a `BOOT` marker record at every start-up carrying the reset reason
- [ ] `logger` at lowest priority; batch to 4–8 KB aligned to the card block size
- [ ] `f_sync` every N seconds and on unmount — never per record
- [ ] `log_q` deep, **drop-oldest with a counter**, never block ⟦6.4⟧
- [ ] Record format: `seq, t_mono_us, t_wall_iso, state, channel, ex, ey, vpan, vtilt, pan, tilt, flags`
- [ ] Timestamps owned by `AIM`: **Phase 0 is monotonic only** — `t_wall_iso` written **empty**, never a placeholder epoch
- [ ] `EYE` notes each `BOOT` marker against its own clock — one line per session converts the whole log to wall time offline, including alignment against the demo video
- [ ] Four failure conditions — no card, removed while running, full, write error. Each logs once, flags the OLED, **never stops the loop** ⟦5.2⟧
- [ ] Mount retried on a slow timer
- [ ] Health telemetry: `sd.present`, `sd.mounted`, `sd.full`, `sd.free_bytes`, `sd.write_errors`, `sd.dropped_records`, `sd.queue_depth`
- [ ] Throughput telemetry: `sd.write_bytes_per_s`, `sd.write_max_latency_us`, `sd.write_p95_latency_us` ⟦6.1⟧

### 5. Performance instrumentation ⟦2.5 6.1 6.3 6.4⟧

- [ ] `esp_timer_get_time()` around the PID step, frame parse and render; min / max / EWMA ⟦6.1⟧
- [ ] `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS` + `uxTaskGetSystemState()` → per-task CPU % ⟦6.4⟧
- [ ] `uxTaskGetStackHighWaterMark()` per task, logged at 1 Hz
- [ ] Toggle a spare GPIO across the control step; capture on the logic analyser, put the screenshot in the README ⟦6.1⟧
- [ ] Write the "data path" paragraph — every copy from camera pixel to servo angle ⟦6.3⟧
- [ ] State the timer/hardware-event configuration explicitly (LEDC, `esp_timer`) ⟦2.5⟧
- [ ] Say **why not** DMA on UART1 — 6.2 rewards judgement, not usage

### 6. Watchdog *(pulled forward — one evening, worth a full item)* ⟦5.1⟧

- [ ] `esp_task_wdt` with `ctrl` and `safety` **subscribed**, ~1 s timeout ⟦5.1⟧
- [ ] Log `esp_reset_reason()` at boot
- [ ] Persist a WDT-reset counter in NVS
- [ ] Laser comes up **off** after any watchdog reset

### 7. Documentation ⟦8.1 8.2 8.3 8.4 8.5 1.4 3.4⟧

> S-04 #3, which you accepted: written *before* the port it changes the port;
> written after, it is transcription.

- [ ] `README.md` — problem, what it does, quick start ⟦8.1 8.4⟧
- [ ] `docs/architecture.md` — node roles, task table, FSM, data path, decisions ⟦8.3⟧
- [ ] Mermaid block diagram + FSM diagram ⟦8.2 1.4⟧
- [ ] **Name which variables cross task boundaries and what protects each one** ⟦3.4⟧
- [ ] `docs/interfaces.md` — every interface: pins, speed, **why chosen**, failure behaviour ⟦8.5⟧
- [ ] `docs/protocol.md` — control ASCII + NDJSON framing and the config plane (MQTT topics land in Phase 1) ⟦8.5⟧
- [ ] `docs/bringup.md` — flash order, first-run checks, expected LED/OLED states ⟦8.4⟧
- [ ] Document the clamps, anti-windup and the `static_assert` — an examiner will not find them in `Config.hpp` ⟦4.4⟧

### 8. Schematic — the `AIM` board ⟦7.1 7.3⟧

- [ ] `AIM` board: ESP32-S3-WROOM-1, USB-C + BQ24040 + TLV758P (from `workshop-5-1`), servo rail, MOSFET laser driver, micro-SD, OLED header, UART1 header, E-stop + **`MODE`** + control buttons (`MODE` on a non-strapping GPIO, pull-up + RC) ⟦7.1⟧
- [ ] Power filtering: bulk on the servo rail sized for stall current, 10 µF + 100 nF per rail, 100 nF at every IC pin, ferrite between servo rail and logic, RC on analog ⟦7.3⟧

### 9. `AIM` board layout — LAST ⟦7.2 7.4 7.5 7.6⟧

> **Worth 3 items.** Reserve a dated slot — roughly two evenings extending the
> `workshop-5-1` layout. Not leftover time.

- [ ] Route 2-layer: signal + fill top, solid ground pour bottom, antenna keep-out ⟦7.2⟧
- [ ] Power/logic separation: separate pours, single-point star ground, servo return never shared with SD or ADC ground ⟦7.4⟧
- [ ] USB D± as a 90 Ω differential pair, length-matched, no stubs or vias on the pair ⟦7.5⟧
- [ ] SD SPI short, continuous return path directly beneath ⟦7.5⟧
- [ ] Test points placed and labelled: 3V3, VSERVO, VBAT, GND ×2, laser gate, SD SCK/MOSI/MISO/CS, UART1 TX/RX ⟦7.6⟧
- [ ] DRC clean, no unwaived errors
- [ ] Front/back copper plots, 3D render, BOM
- [ ] `hardware/aim-board/DESIGN-REVIEW.md` — the four or five decisions actually made ⟦9.4⟧

### 10. Reliability hardening — LAST ⟦5.2 5.4 9.1⟧

> **Worth 2.5 items.** Wants a running system to harden, hence last — but reserve
> the slot.

- [ ] `EYE`: reopen the serial port when it reappears; do not exit when it dies ⟦5.2 9.1⟧
- [ ] `EYE`: rebuild the DepthAI pipeline on camera loss instead of ending the loop ⟦9.1⟧
- [ ] `EYE`: replace the two bare `except Exception: pass` with logging that keeps the loop alive ⟦5.2⟧
- [ ] `AIM`: OLED missing or NAKing → log once, disable `ui`, **keep controlling** ⟦5.2⟧
- [ ] `eye/tools/fuzz_link.py` — truncated lines, 10 kB lines, NUL bytes, binary noise, `NaN`, `1e300`, half a frame then reset, stale replays ⟦5.4⟧
- [ ] Success criterion: counters increase, uptime does not reset, gimbal never moves on garbage ⟦5.4⟧
- [ ] Boot self-test gating `ARMED`: I²C scan, SD mount, servo micro-sweep, camera handshake. `FAIL` blocks arming, `ABSENT` degrades gracefully ⟦5.2⟧
- [ ] 30-minute unattended soak: fps, free heap, stack high-water marks, per-task CPU, error counters, reconnects, WDT resets. Commit the log and a plot ⟦9.1⟧

### 11. Demo video ⟦9.1 9.2 9.3 9.4⟧ — *the accepted shot list — shoot to it*

- [ ] Power on → self-test → zone tour, direction check explained ⟦9.3⟧
- [ ] Arm → tracking → move the target by hand ⟦9.1⟧
- [ ] `N` nudge disturbance → recovery, telemetry graph visible ⟦9.2⟧
- [ ] Channel switch via an NDJSON config line over UART1 → `AUTO` yields to `MANUAL`, PIDs reset ⟦9.2 9.3⟧
- [ ] Unplug the data link → failsafe → `MODE` button still cycles the channel with no link at all, OLED confirms → reconnect and re-sync ⟦9.2 5.2⟧
- [ ] Pull the SD card mid-track → `sd.present` false, gimbal keeps tracking ⟦9.2⟧
- [ ] Garbage on UART1 → error counters climb, uptime does not ⟦9.2⟧
- [ ] **One unbroken 5-minute take with the uptime counter on screen** ⟦9.1⟧
- [ ] Screen recording: schematic walkthrough, task table, FSM diagram ⟦9.3 9.4⟧
- [ ] Keep a fallback recording of a good run

### Phase 0 gate

- [ ] **Set the deadline date** — 5.5 items sit in steps 9 and 10, after this gate
- [ ] Demo end-to-end before opening Phase 1
- [ ] Update the coverage table in `REVIEW.md` §5 against what actually shipped

---

## Phase 1 — opens only after Phase 0 demos end-to-end

### `PILOT` firmware and board — first

- [ ] `firmware/pilot/` — 2-axis joystick, ESP32-C3
- [ ] `adc_continuous` (DMA-backed) + SMA filter from `workshop-3-2` ⟦6.2 2.1⟧
- [ ] `EspNowTransport` behind `ITransport`; `radio` task on `AIM`
- [ ] `PILOT` input channel added to the exclusive selector — and to the `MODE` button's cycle
- [ ] Remote E-stop — out-of-band, same handling as the local button
- [ ] Deep sleep: auto-sleep after 30 s idle, GPIO wake on the joystick button
- [ ] Counters and session ID in RTC slow memory across sleeps
- [ ] Battery voltage on ADC; low-battery warning to the `AIM` OLED
- [ ] Measure and report actual sleep current
- [ ] `PILOT` board **schematic**: ESP32-C3-MINI-1, charger + LDO, joystick with RC filtering, E-stop button, optional OLED and nRF24 headers, battery connector — moved here from Phase 0, since 7.1 is answered by the `AIM` schematic alone
- [ ] `PILOT` board layout — *the correct thing to sacrifice if the calendar slips*

### MQTT config plane and network telemetry

> Moved out of Phase 0 whole: 4.3 is answered by the NDJSON config plane over
> UART1, and 2.2 is answered three times over without it.

- [ ] `MqttTransport` behind `ITransport`; `link_net` task on `AIM`
- [ ] Fill in `mqtt/config/mosquitto.conf`; keep credentials out of the repo
- [ ] MQTT reconnect with backoff + Last Will
- [ ] `.../config/set` published **non-retained** so a returning broker cannot replay a stale channel over the `MODE` button; `.../config/state` retained and re-published on reconnect
- [ ] MQTT topic map added to `docs/protocol.md`
- [ ] WiFi telemetry gated on `telemetry.wifi.enabled`, independent of SD logging

### Laser hardware

- [ ] MOSFET low-side switch replacing the relay
- [ ] External pull-up on the driver input — covers the pre-firmware window
- [ ] Verify with a scope across power-on, reset and flash
- [ ] PWM brightness via `drivers/PWM.hpp`; fire must restore the *set* level, not full on
- [ ] Brightness letter in the protocol + slider in the `EYE` controls window

### `VAULT` storage node

- [ ] `firmware/vault/` — STM32F401, **SPI master on both buses**: master to the SD card *and* master on the link to `AIM`
- [ ] `AIM` side: **SPI slave** with DMA, two buffers queued at all times, 4-byte aligned, length a multiple of 4
- [ ] `DRDY` GPIO `AIM` → `VAULT`; `VAULT` clocks a frame when `DRDY` is asserted and the card is ready
- [ ] One fixed-size full-duplex frame: record out on MISO, `VAULT` status word back on MOSI
- [ ] `AIM` liveness timeout — no completed transaction within N ms while `DRDY` is high → `vault.link_lost`, fall back to `AIM`'s own card
- [ ] Start the link clock conservative (ESP32 **slave** mode sets the ceiling, not the STM32); measure before raising it
- [ ] Keep the two SPI buses separate — do not share the card's bus with the link
- [ ] No parsing, no timestamps, no state — writes fully-formed records verbatim
- [ ] `F-22` rotation logic ported across unchanged
- [ ] Status back to `AIM`: card present, free space, write errors, current segment
- [ ] `IWDG`, refreshed only while card and link are healthy
- [ ] BME280 + servo-rail current sense so the log has something worth reading

### Other

- [ ] **Wall clock** — SNTP as the source, `EYE` set-time over NDJSON as the offline fallback (DS3231 declined: extra part, coin cell, second device on the OLED I²C bus)
- [ ] `TIMESET` event record on acquisition — never rewrite past records, never step monotonic time
- [ ] **Log rotation** — `N` pre-created fixed-size segments `LOG0000.CSV …` + `INDEX.TXT`, overwritten in place, never deleted and recreated; segment header carries its sequence number; `sd.segment` added back to health telemetry
- [ ] `boot_id` counter in NVS written into every segment header — `(boot_id, seq)` orders segments across a wrap and a reboot
- [ ] SD DMA on `AIM`, if not already covered in Phase 0
- [ ] `INJECT` fault-injection console — `link_drop`, `corrupt`, `stall`, `i2c_fail`, `heap`
- [ ] BLE telemetry transport, config-gated
- [ ] On-device OLED gain-editing menu using the joystick
- [ ] CSRT discussion — try `cv2.TrackerCSRT_create()` from `opencv-contrib-python` first

---

## Phase 2 — cut without regret

- [ ] NRF24L01+ behind `ITransport` — timeboxed to one evening
- [ ] On-camera YOLO detection (OAK-1 RVC2 blobs)
- [ ] Target-velocity feed-forward
- [ ] `PILOT` OLED
- [ ] Gain scheduling if plant gain `k` varies across travel
- [ ] Acceleration limit as well as rate limit
- [ ] Preserve angles across reboots

---

## Open questions

- [ ] **Q-7** — Phase 0 deadline date. Both last tasks depend on it
- [ ] **DMA** — does your ESP-IDF version give the `sdspi` bus a DMA channel at `spi_bus_initialize`? Requirement 6.2 rests on it
- [ ] **Q-3** — is 6.4 "CPU < 70%" or "shown not to be overloaded"?
- [x] **Q-8** — wall-clock source — **answered: none in Phase 0** (monotonic only, one `BOOT` marker per run); **SNTP + `EYE` fallback in Phase 1**
- [x] **Q-9** — local override for `input.channel` when the config plane is down — **answered: `MODE` button on `AIM`, cycles to the next channel**. Note the premise moved: with MQTT in Phase 1 the button now covers a dead **UART1 link**, not a dead broker
- [ ] **Q-11** — log retention: segment size × count *(Phase 1, with rotation)*
- [x] **Q-5** — parts on hand: joystick, SD module, ESP32-C3, servos — **all owned**
- [x] **R-25** — demo shot list **accepted**; §11 is the list to shoot to
