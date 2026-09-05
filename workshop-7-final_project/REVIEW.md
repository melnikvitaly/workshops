# Final Project — Plan Review

Reviewer role: experienced embedded developer, reviewing the plan in
[`README.md`](./README.md) against the course verification sheet
([`VERIFICATION/Requirements.html`](./VERIFICATION/Requirements.html)) and against
the topics actually covered in workshops 1–6 of this repo.

**Revision 3** — round-2 answers folded in. All findings are now resolved except
**R-13**. [`README.md`](./README.md) has been updated to match.

**How to use this document.** Every point has a stable ID (`R-nn` findings,
`F-nn` feature proposals, `S-nn` scope decisions, `Q-nn` questions).

- **`✅ RESOLVED`** — your decision and what it changed. Nothing to do unless I
  applied it in a way you did not intend.
- **`⏳ OPEN`** — empty `Decision:` block, still needs an answer.
- **`🆕 NEW`** — consequences of your round-2 answers.

---

## 0. Where the plan stands

### Phase 0 score: 33.5 / 42 = **79.8%**

**You are 0.2 percentage points under the bar.** Half an item.

| | Weighted | % |
|---|---|---|
| Plan as originally written | 12 / 42 | 29% |
| After round 1 | 25.5 / 42 | 61% |
| **After round 2 (Phase 0 as now defined)** | **33.5 / 42** | **79.8%** |
| Pass bar | 33.6 / 42 | 80% |

Per block, Phase 0 only:

| Block | Max | Phase 0 | What moved |
|-------|-----|---------|------------|
| 1. Architecture | 5 | **5** ↑ | R-12 + R-16 accepted — FSM and diagram now in |
| 2. Peripherals | 5 | **5** ↑ | 2.5 gets stated via R-15 |
| 3. RTOS | 4 | **4** ↑ | 3.4 write-up lands with the docs |
| 4. Control | 4 | 4 | — |
| 5. Reliability | 4 | **2** | **R-13 still unanswered** |
| 6. Performance | 4 | **4** ↑↑ | R-15 accepted — biggest single gain, +3.5 |
| 7. PCB | 7 | **3** ↓↓ | **Layouts moved to Phase 1, no fabrication — lost 3.5** |
| 8. Documentation | 5 | **5** ↑↑ | R-16 accepted, +4 |
| 9. Demonstration | 4 | **1.5** | R-17 rejected |
| **Total** | **42** | **33.5** | **79.8%** |

Because the sheet is a checkbox per row, partials resolve one way or the other at
grading:

| Reading | Score | |
|---|---|---|
| Strict (every partial → no) | 30 / 42 | **71.4%** |
| Weighted midpoint | 33.5 / 42 | **79.8%** |
| Generous (every partial → yes) | 37 / 42 | **88.1%** |

So it is genuinely borderline rather than safely passing. Three levers, any **one**
of which clears the bar:

| Lever | Gain | Result |
|---|---|---|
| **Answer R-13** (the only open finding) | +2.5 | 36.0 / 42 = **85.7%** |
| **Move PCB layouts back into Phase 0** (R-26) | +3.0 | 36.5 / 42 = **86.9%** |
| **A shot list for the demo video** (R-25) | +2.5 | 36.0 / 42 = **85.7%** |
| All three | — | ≈ 41 / 42 = **98%** (only 7.7 fabrication missing) |

Round 2 was a large net gain — R-15 and R-16 together added 7.5 items. The PCB
re-phasing gave 3.5 of that back, and R-17's rejection left 2.5 on the table.

---

## 1. Decisions log

### Round 1

| ID | Decision | Effect |
|----|----------|--------|
| R-01 | accept | Phase 0/1/2 with a hard cut line |
| R-02 | accept | Two boards — schematic, then layout |
| R-03 | accept | FreeRTOS; F-01 adopted |
| R-04 | accept | STM32 = medium only; timestamps on S3; transmission config-gated |
| R-05 | accept | SD + FatFs, rolling files, overwrite oldest |
| R-06 | **reject** | STM32 out of Phase 0; S3 writes SD directly |
| R-07 | accept | ESP-NOW primary; NRF24 → Phase 2 |
| R-08 | accept | UART1 for data, UART0 console-only |
| R-09 | accept | Compact ASCII control path, NDJSON config/telemetry |
| R-10 | accept | S3 `PARKED`; deep sleep only on C3 |
| R-11 | accept | One exclusive channel, MQTT-selected |
| R-18 | accept | Copy files; fully autonomous project |
| R-20 | accept | `ITransport`, ESP-NOW default |

### Round 2

| ID | Decision | Your note | Effect |
|----|----------|-----------|--------|
| R-12 | accept | — | FSM + diagram in Phase 0. **+2 items** |
| R-14 | accept | *"add this as Tier1 TODO"* | Hardware half → Phase 1. Firmware half stays Phase 0 — see below |
| R-15 | accept | *"telemetry: speed of writing to SD-CARD; DMA for SD as Tier1"* | **+3.5 items** — the biggest gain in round 2 |
| R-16 | accept | *"for now we work on plan, architecture diagram later"* | **+4 items**. Deferred but committed |
| R-17 | **reject** | *"i will have video with the DEMO"* | Accepted. Raises **R-25** — the video still has to *contain* 9.1–9.4 |
| R-19 | discuss | *"place it in Tier1"* | CSRT → Phase 1, revisit later |
| R-21 | accept | — | Preserved through the FreeRTOS port |
| R-22 | accept | — | E-stop out-of-band on every transport |
| R-23 | accept | — | `log.sd.enabled` independent of `telemetry.*.enabled` |
| R-24 | accept | *"also one of the telemetry the S3 sends over channels"* | SD health added to the telemetry schema |
| S-01 | accept | *"Phase 0 schematics for both; Phase 1 layouts for both"* | **−3.5 items** from Phase 0 — see **R-26** and **Q-13** |
| S-04 | partial | Accept #3 only; no fabrication; #1 on Phase 1; #4 yes | 7.7 = no. Deadline pressure on R-14 removed |

### Round 3

| ID | Decision | Effect |
|----|----------|--------|
| Node naming | requested | Nodes get **role names** independent of silicon: `EYE` (vision, PC), `AIM` (control, ESP32-S3), `PILOT` (remote, ESP32-C3), `VAULT` (storage, STM32). Used as log tags, MQTT topic segments and source directories. Input channels renamed `AUTO` / `MANUAL` / `PILOT` / `NONE`; boards renamed `AIM` board / `PILOT` board. Contributes to 1.5 and 8.3 |

---

## 2. Round-2 resolutions

### R-12 — State machine `✅ RESOLVED — accepted` `+1.3, 1.4`

**F-02** becomes the system's control structure: `BOOT → SELFTEST → ZONE_TOUR →
DISARMED → ARMED`, plus `PARKED` (R-10), `LINK_LOST` (selected channel stale) and a
latched `FAULT` requiring operator acknowledgement. Mermaid diagram in the README.

### R-14 — Laser safety `✅ RESOLVED — accepted, hardware half deferred`

> *"but lets add this as Tier1 TODO"*

Fine for the **hardware** half, and your S-04 answer makes it safer than it looked:
with no fabrication, the schematic can change any time before the deadline, so the
"decide before routing" pressure I flagged in revision 2 is gone. I'm withdrawing
that as the most time-critical item.

**One split I'd keep, and it costs you nothing:** R-14 has two halves with very
different price tags.

| Half | Cost | Proposed phase |
|---|---|---|
| Firmware — `gpio_set_level()` before `gpio_config()`, internal pull-up, interlock in `safety` | **~5 lines** | **Phase 0** |
| Hardware — MOSFET replacing the relay, external pull-up, PWM brightness | schematic + parts | Phase 1 ✔ your call |

The firmware half is an actual defect in code you are copying — the pin is briefly
driven into the energised state during `init()`, and floats before it. Five lines in
Phase 0 removes the code-side half of a laser that lights when it should not. The
board-side half can absolutely wait.

### R-15 — Performance measurement `✅ RESOLVED — accepted` `+6.1, 6.3, 6.4; 6.2 →Y`

> *"Lets have as part of telemetry speed of writing data to SD-CARD. Also plan as
> Tier1 to use DMA for communication with SD-CARD if possible"*

Best-value answer in round 2 — **+3.5 items** for work that is almost entirely
instrumentation. And SD write throughput as telemetry is a better idea than what I
proposed: it is a *continuously observable* critical-section measurement rather than
a one-off number in the README, which is exactly what 6.1 asks for. Added to the
telemetry schema:

```text
sd.write_bytes_per_s      sd.write_max_latency_us     sd.write_p95_latency_us
sd.queue_depth            sd.dropped_records          sd.sync_count
```

`sd.write_max_latency_us` is also your early warning for the stall problem from
R-05 — you will *see* a 200 ms card stall in telemetry instead of discovering it as
a frozen gimbal.

**On DMA (6.2) — one caution.** Deferring SD DMA to Phase 1 is reasonable, but it
means 6.2 in Phase 0 rests entirely on the **C3 joystick's `adc_continuous`**, which
is DMA-backed. That is enough, *provided the C3 firmware lands in Phase 0*. If the
C3 slips, 6.2 drops to N and Phase 0 loses another item. Cheapest insurance:
ESP-IDF's `sdspi` host can use DMA with essentially a configuration change rather
than a rewrite — worth ten minutes to check before assuming it is Phase 1 work.

### R-16 — Documentation `✅ RESOLVED — accepted` `+8.1–8.5`

> *"for now we will work on plan, architecture diagram will be later written"*

**+4 items**, the second-biggest gain. Deferring the writing is fine; the commitment
is what scores. Worth remembering S-04 #3 — which you accepted — says
`docs/architecture.md` written *before* the FreeRTOS port will change the port,
whereas written after it is transcription. "Later" is fine; "after everything is
built" costs you the benefit you just agreed to.

### R-17 — Demonstration `✅ RESOLVED — rejected`

> *"i will have video with the DEMO"*

Accepted — the video is the demo, and rehearsal-for-a-live-demo is not your
situation. See **R-25**: block 9 is still scored on *what the video contains*, and
that is now a much smaller ask than the original F-18.

### R-19 — CSRT `✅ RESOLVED — deferred to Phase 1 for discussion`

Noted. The cheap experiment when you get there: `cv2.TrackerCSRT_create()` from
**opencv-contrib-python** — an hour, versus porting the ROS/OpenCV-3.3 repo.

### R-21 — Positives `✅ RESOLVED — accepted`

Preserved through the FreeRTOS port: the camera-in-the-loop feedback, runtime gain
tuning with the `N` nudge, the clamps and `static_assert`, the boot zone tour.

### R-22 — E-stop exempt from channel selection `✅ RESOLVED — accepted`

E-stop is not a channel: a distinct message accepted from any transport, in any
state, regardless of `input.channel`, handled by `safety`, latching `FAULT`.

### R-23 — Independent storage/transmission switches `✅ RESOLVED — accepted`

`log.sd.enabled = true` by default; `telemetry.wifi.enabled` and
`telemetry.ble.enabled` default off. Turning transmission off never stops recording.

### R-24 — SD as a failure-prone peripheral `✅ RESOLVED — accepted, extended`

> *"i expect that this also will be one of the telemetry that ESP32-S3 sends over channels"*

Agreed and folded into the schema alongside R-15's throughput fields:

```text
sd.present   sd.mounted   sd.full   sd.write_errors   sd.segment   sd.free_bytes
```

Two useful consequences: the four failure conditions become *observable* rather than
just handled, which is what 5.2 actually asks for; and pulling the card mid-track
while the PC watches `sd.present` go false and the gimbal keep tracking is a 20-second
segment of the video that scores 9.2.

### S-01 — Phase 0 `✅ RESOLVED — accepted, PCB re-phased`

> *"but at Phase1, schematics for Joystic and ESP-s3, Phase1 - layouts for both"*

Read as: **Phase 0 = schematics for both boards; Phase 1 = layouts for both.** That
is the only reading that makes the sentence consistent with R-02's "schematic and
next step PCB layout" — but it is a typo-level ambiguity on the single highest-value
block in the sheet, so please confirm (**Q-13**). The cost is quantified in **R-26**.

### S-04 — Ordering constraints `✅ RESOLVED — partial`

> *"Accept only #3, i am not going to order fabrication of PCB, i will work with #1
> on tier 1. #4 - yes i need to have Phase0 until some date"*

| # | Constraint | Status |
|---|---|---|
| 1 | MOSFET decision before routing | **Dropped** — no fabrication means no routing deadline. Correctly de-prioritised |
| 2 | PCB early for lead time | **Moot** — nothing is being ordered |
| 3 | Documentation early | **Accepted** ✔ |
| 4 | Phase 0 gated by a date | **Accepted** — still needs the date (**Q-7**) |

This also answers **Q-2**: no fabrication, so **7.7 = no**, permanently. That is
0.5–1 item you have chosen to spend, which is a defensible trade for the cost and
lead time of a board order.

---

## 3. New findings

### R-25 — The video still has to contain 9.1–9.4 `🆕 NEW` `severity: medium` `worth +2.5`

You rejected R-17 on the grounds that there will be a demo video. Agreed — but the
four items in block 9 are scored on *content*, not on the existence of a recording:

| # | Requirement | Satisfied by a video only if it… |
|---|---|---|
| 9.1 | Runs stably ≥ 5 min | contains ≥5 minutes of continuous unedited operation, or a visible uptime counter |
| 9.2 | Boundary modes demonstrated | shows the limits being hit on purpose |
| 9.3 | Architecture explained | has narration or captions covering it |
| 9.4 | PCB decisions explained | shows the schematic and explains the choices |

So R-17's *finding* survives its rejection; only the remedy shrinks. Instead of
F-18's soak-plus-rehearsal, all this needs is **a one-page shot list written before
you record**, which costs an hour and recovers ~2.5 items:

| # | Shot | Scores |
|---|------|--------|
| 1 | Power on → self-test → zone tour, direction check explained | 9.3 |
| 2 | Arm → tracking → move the target by hand | 9.1 |
| 3 | `N` nudge disturbance → recovery, telemetry graph visible | 9.2 |
| 4 | Channel switch via MQTT config → joystick takes over | 9.2, 9.3 |
| 5 | Unplug the data link → failsafe → reconnect | 9.2 |
| 6 | **Pull the SD card mid-track** → `sd.present` false, gimbal keeps tracking | 9.2 |
| 7 | Feed garbage on UART1 → counters climb, uptime does not | 9.2 |
| 8 | Uninterrupted 5-minute tracking segment with the uptime counter on screen | **9.1** |
| 9 | Screen recording: schematic walkthrough + task table + FSM diagram | 9.3, 9.4 |

Shot 8 is the one people forget. An edited highlight reel does not demonstrate
stability — one unbroken five-minute take does, and it is the cheapest item in the
whole sheet.

```text
Decision: [ ] accept   [ ] reject   [ ] discuss
Notes:
```

### R-26 — Moving PCB layout to Phase 1 costs 3.5 items `🆕 NEW` `severity: high` `worth +3.0`

This is what put Phase 0 under the bar. Block 7 is the largest block in the sheet
(7 of 42), and four of its seven items can only be evidenced by a **layout**:

| # | Requirement | Schematic alone | Needs layout |
|---|-------------|-----------------|--------------|
| 7.1 | Schematic created | **Y** | — |
| 7.2 | 2-layer PCB routed | N | ✔ |
| 7.3 | Power filtering | **Y** (caps, ferrites are schematic decisions) | — |
| 7.4 | Power/logic separated | P (rails are schematic; pours and star ground are not) | ✔ |
| 7.5 | High-speed routing | N (differential pairs, return paths, keep-outs) | ✔ |
| 7.6 | Test points | P (symbols on a schematic; placement is layout) | ✔ |
| 7.7 | Fabricated | N — you have chosen not to | — |
| | **Phase 0 total** | **3 / 7** | |

**Recommendation: route one board in Phase 0 — Board A only.** Not both. That is
+3.0 items and takes Phase 0 from 79.8% to 86.9% on its own. Board B's schematic
still lands in Phase 0 as you planned; only its layout waits.

Two supporting reasons beyond the arithmetic:

- **You are not fabricating**, so a layout has no lead time and no cost. It is
  purely drawing time, and KiCad DRC gives you the "routed correctly" evidence
  without a board ever existing.
- **`workshop-5-1` is most of Board A already** — a routed 2-layer ESP32-S3 board
  with the same USB-C + BQ24040 + TLV758P power chain, a DRC report and a design
  review. You are extending a layout, not starting one.

If layout genuinely cannot fit Phase 0, the fallback that recovers half of it: put
**test point symbols and a documented power-domain split on the schematic**, which
firms 7.4 and 7.6 from P to Y — +1.0 for perhaps an hour.

```text
Decision: [ ] accept   [ ] reject   [ ] discuss
Notes:
```

---

## 4. Open findings

**One left.**

### R-13 — No reliability plan, and the base project is documented as untested `⏳ OPEN` `severity: high` `governs 5.1, 5.2, 5.4, 9.1` `worth +2.5`

This is now the **only** unanswered finding and the single largest item on the
table — worth +2.5, which alone takes Phase 0 from 79.8% to 85.7%.

`workshop-5-miniproject`'s own TODO records that nothing reconnects: after the ESP32
re-enumerates, the PC side prints *Serial write failed* forever and the gimbal sits
in failsafe until the script is restarted by hand; if the camera drops,
`camera_frames()` stops yielding and the app exits; two bare `except Exception: pass`
swallow tracebacks.

It matters more now than in revision 1, for two reasons that came out of your own
answers:

- **Shot 8 of R-25 is an unbroken five-minute take.** With the PC side unable to
  survive a USB hiccup or a camera dropout, that take is a coin flip. The reliability
  work and the 9.1 evidence are the same work.
- **R-24 already committed you to half of it.** The four SD failure conditions are
  peripheral error handling — accepting R-13 mostly means applying the same
  discipline to the serial port and the camera, which is where it is currently absent.

What it buys, item by item:

| Feature | Effort | Closes |
|---|---|---|
| **F-06** — `esp_task_wdt` with `ctrl`+`safety` subscribed, reset-reason logging, WDT counter in NVS | S | 5.1 (currently N) |
| **F-07** — PC-side reconnect: reopen the serial port, rebuild the DepthAI pipeline, log instead of swallowing | M | 5.2 → Y, 9.1 → Y |
| **F-05** — `tools/fuzz_link.py`, malformed input against the framing contract | S | 5.4 → Y |
| **F-19** — boot self-test gating `ARMED` (I2C scan, SD mount, servo sweep, radio ping) | S | reinforces 5.2, 9.2 |

```text
Decision: [ ] accept   [ ] reject   [ ] discuss
Notes:
```

---

## 5. Requirement coverage — Phase 0

Legend: **Y** covered · **P** partial · **N** not addressed. Arrows show movement
since revision 2.

### 1. Архітектура — 5 / 5 ↑

| # | Requirement | Rev 2 | Now | Why |
|---|-------------|-------|-----|-----|
| 1.1 | Superloop or RTOS | Y | Y | FreeRTOS (R-03) |
| 1.2 | No blocking `delay()` | Y | Y | — |
| 1.3 | State machine | N | **Y** ↑ | R-12 accepted → F-02 |
| 1.4 | Architecture diagram | N | **Y** ↑ | R-12 + R-16 accepted |
| 1.5 | Split into modules | Y | Y | `ITransport`, task-per-file |

### 2. Периферія та інтерфейси — 5 / 5 ↑

| # | Requirement | Rev 2 | Now | Why |
|---|-------------|-------|-----|-----|
| 2.1 | ≥1 analog subsystem | Y | Y | Servo PWM + C3 joystick ADC |
| 2.2 | ≥1 digital interface | Y | Y | UART1, I²C, SPI |
| 2.3 | Interrupts | Y | Y | Buttons, E-stop |
| 2.4 | Async transfer | Y | Y | `cmd_q`, `log_q` |
| 2.5 | Timers / hardware events | P | **Y** ↑ | Stated as part of R-15's instrumentation |

### 3. RTOS — 4 / 4 ↑

| # | Requirement | Rev 2 | Now | Why |
|---|-------------|-------|-----|-----|
| 3.1 | ≥2 tasks | Y | Y | Seven tasks |
| 3.2 | Queue | Y | Y | `cmd_q`, `log_q` |
| 3.3 | Mutex / synchronisation | Y | Y | Config mutex, task notifications |
| 3.4 | No race conditions | P | **Y** ↑ | The write-up lands with R-16's docs |

### 4. Керування та алгоритми — 4 / 4

| # | Requirement | Rev 2 | Now | Why |
|---|-------------|-------|-----|-----|
| 4.1 | Feedback | Y+ | Y+ | Camera-in-the-loop |
| 4.2 | PID | Y+ | Y+ | Per-axis, filtered D, anti-windup |
| 4.3 | Parameters configurable | Y | Y | Config plane (F-23) |
| 4.4 | Boundary conditions | Y | Y | Clamps + `static_assert`, documented via R-16 |

### 5. Надійність — 2 / 4 `R-13 open`

| # | Requirement | Rev 2 | Now | Why |
|---|-------------|-------|-----|-----|
| 5.1 | WDT | N | **N** | **R-13 open** — F-06 not committed |
| 5.2 | Peripheral error handling | P | P | SD conditions covered (R-24); serial and camera are not |
| 5.3 | Invalid data validated | Y | Y | Framing contract (R-09) |
| 5.4 | No hang on bad input | P | P | Needs F-05 (**R-13**) |

### 6. Продуктивність — 4 / 4 ↑↑

| # | Requirement | Rev 2 | Now | Why |
|---|-------------|-------|-----|-----|
| 6.1 | Critical-section timing | N | **Y** ↑ | R-15 — SD write latency in telemetry, `esp_timer` on the PID step |
| 6.2 | DMA where appropriate | P | **Y** ↑ | C3 `adc_continuous`. **Depends on the C3 landing in Phase 0** |
| 6.3 | No redundant copies | N | **Y** ↑ | R-15 → F-10 data-path paragraph |
| 6.4 | CPU not overloaded | N | **Y** ↑ | R-15 → run-time stats on the OLED |

### 7. PCB — 3 / 7 ↓↓ `R-26`

| # | Requirement | Rev 2 | Now | Why |
|---|-------------|-------|-----|-----|
| 7.1 | Schematic | Y | Y | Both boards, Phase 0 |
| 7.2 | 2-layer routed | Y | **N** ↓ | Layout → Phase 1 (S-01) |
| 7.3 | Power filtering | Y | Y | Schematic-level decision |
| 7.4 | Power/logic separated | Y | **P** ↓ | Rails on the schematic; pours and star ground need layout |
| 7.5 | High-speed routing | Y | **N** ↓ | Needs layout |
| 7.6 | Test points | Y | **P** ↓ | Symbols yes, placement no |
| 7.7 | Fabricated | P | **N** ↓ | S-04 — not ordering. Settled |

### 8. Документація — 5 / 5 ↑↑

| # | Requirement | Rev 2 | Now | Why |
|---|-------------|-------|-----|-----|
| 8.1 | README with task description | P | **Y** ↑ | R-16 |
| 8.2 | Block diagram | N | **Y** ↑ | R-16 |
| 8.3 | Architecture description | N | **Y** ↑ | R-16 |
| 8.4 | Startup instructions | P | **Y** ↑ | R-16 |
| 8.5 | Interfaces explained | N | **Y** ↑ | R-16 + the README's interface table |

### 9. Демонстрація — 1.5 / 4 `R-25`

| # | Requirement | Rev 2 | Now | Why |
|---|-------------|-------|-----|-----|
| 9.1 | Stable ≥ 5 min | RISK | P | Video planned; **R-13 open** makes an unbroken take a gamble |
| 9.2 | Boundary modes | N | N | No shot list — **R-25** |
| 9.3 | Architecture explained | N | P | Material exists via R-16; needs narration |
| 9.4 | PCB decisions explained | N | P | Schematic decisions exist; no `DESIGN-REVIEW.md` |

**Total: Y = 30 · P = 7 · N = 5 → 33.5 / 42 = 79.8%.**

---

## 6. Feature status

### Adopted — in the plan

| ID | Feature | From |
|----|---------|------|
| F-01 | FreeRTOS task architecture | R-03 |
| F-02 | System FSM + Mermaid diagram | **R-12** |
| F-03 | Exclusive input-channel selector | R-11 |
| F-04 | `ITransport`, ESP-NOW default | R-20 |
| F-08 | Timing, run-time stats, stack high-water marks | **R-15** |
| F-09 | DMA — C3 `adc_continuous` (Phase 0), SD SPI (Phase 1) | **R-15** |
| F-10 | Zero-copy discipline + data-path paragraph | **R-15** |
| F-15 | PCB — schematics Phase 0, layouts Phase 1 | R-02, S-01 |
| F-16 | STM32 storage controller, Phase 1 | R-04, R-06 |
| F-17 | Documentation set | **R-16** |
| F-22 | SD rolling-file logging + health/throughput telemetry | R-05, **R-15**, **R-24** |
| F-23 | Configuration plane | R-04, R-11 |
| F-14a | Laser boot-safe GPIO + interlock — **firmware half** | **R-14** |

### Deferred to Phase 1

`F-14b` MOSFET laser driver · `F-16` STM32 · `F-21` fault injection · SD DMA ·
BLE transport · Board B layout · CSRT (R-19)

### Blocked on R-13

| ID | Feature | Closes |
|----|---------|--------|
| F-05 | `tools/fuzz_link.py` | 5.4 |
| F-06 | `esp_task_wdt`, reset-reason logging | 5.1 |
| F-07 | PC-side reconnect, no swallowed exceptions | 5.2, 9.1 |
| F-19 | Boot self-test gating `ARMED` | 5.2, 9.2 |

### Reshaped by R-25

`F-13` (`TEST` command suite) and `F-18` (soak + rehearsal) collapse into **the
nine-shot list** in R-25 — much less work, most of the score.

---

## 7. Phases

### S-01 — Phase 0 `✅ accepted`

PC + ESP32-S3 + SD card + ESP32-C3 joystick. **No STM32.**

- Copy the base into an autonomous tree (R-18)
- FreeRTOS port (F-01), config plane (F-23), exclusive channel selector (F-03)
- UART1 data link + framing contract (F-04, R-08, R-09)
- SD logging with rolling files, health and throughput telemetry (F-22, R-15, R-24)
- ESP-NOW joystick, C3 deep sleep, `adc_continuous` (F-04, F-11) — **6.2 depends on this**
- FSM + diagram (F-02), performance instrumentation (F-08–F-10), documentation (F-17)
- Laser boot-safe GPIO + interlock, firmware only (F-14a)
- **Schematics for both boards** — and, per **R-26**, ideally Board A's layout too

### S-02 — Phase 1

Board A + B layouts · MOSFET laser driver (F-14b) · STM32 storage controller (F-16) ·
SD DMA · fault injection (F-21) · BLE transport · CSRT discussion (R-19)

### S-03 — Phase 2

NRF24L01+ · on-camera YOLO · target-velocity feed-forward (F-20) · joystick OLED ·
PWM laser brightness

### S-04 — Ordering constraints `✅ partial`

Only #3 survives: **documentation early, not last.** #1 and #2 are moot without
fabrication; #4 stands but still needs its date (**Q-7**).

---

## 8. Open questions

| # | Question | Status |
|---|----------|--------|
| **Q-13** | **Confirm the PCB phasing** — schematics both boards in Phase 0, layouts both in Phase 1? Your note says "Phase1" twice, which I read as a typo | **Open — worth 3 items (R-26)** |
| **Q-14** | Will the C3 joystick firmware definitely be in Phase 0? | **Open** — 6.2 rests on its `adc_continuous` |
| Q-3 | Is 6.4 "CPU < 70%" or "shown not overloaded"? | Open — changes what you report |
| Q-5 | Do you own the joystick, SD module, C3, servos? | Open |
| Q-7 | Phase 0 deadline date? | Open — S-04 #4 depends on it |
| Q-8 | Wall-clock source: SNTP, DS3231, or PC-set? | Open — monotonic works meanwhile |
| Q-9 | Local override for `input.channel` when the broker is down? | Open |
| Q-11 | Log retention (segment size × count)? | Open |
| ~~Q-2~~ | ~~PCB fabricated?~~ | **Answered — no. 7.7 = N** |
| ~~Q-4~~ | ~~Superloop argument?~~ | **Moot — FreeRTOS** |
| ~~Q-10~~ | ~~Which board if only one?~~ | **Answered — both schematics** |
| ~~Q-12~~ | ~~C3 in Phase 0?~~ | Superseded by **Q-14** |

---

## 9. Summary

**Round 2 was a big net gain.** R-15 (+3.5) and R-16 (+4) are the two best-value
answers in either round — 7.5 items for work that is instrumentation and prose, no
new hardware. R-12 added 2 more.

**But Phase 0 lands at 79.8%, half an item under the bar.** Three things put it
there, and each has a cheap fix:

1. **R-13 is still unanswered** — the only open finding, worth +2.5. Its two halves
   are a watchdog on the S3 and reconnection logic on the PC. Note that it is also
   what makes R-25's unbroken five-minute take reliable rather than a gamble.
2. **PCB layout moved to Phase 1** (R-26), worth +3.0. You are not fabricating, so
   a layout costs drawing time only — and `workshop-5-1` is most of Board A already.
   Routing **one** board in Phase 0 is the recommendation; both is not needed.
3. **R-17's rejection** left block 9 at 1.5/4. Your reasoning was sound, and the
   remedy shrinks to **a one-page shot list** (R-25) worth +2.5 — most importantly
   one unbroken five-minute take for 9.1.

Any one of the three clears 80%. All three put you at roughly 41/42, with only
fabrication (7.7) deliberately unspent.

**One thing I'd still do regardless of R-13's answer:** the five-line firmware half
of R-14 — `gpio_set_level()` before `gpio_config()`, plus the internal pull-up. The
board-side MOSFET can wait for Phase 1 as you decided; the code-side defect is in
the files you are about to copy.
