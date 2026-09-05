# Final Project — Plan Review

Reviewer role: experienced embedded developer, reviewing the plan in
[`README.md`](./README.md) against the course verification sheet
([`VERIFICATION/Requirements.html`](./VERIFICATION/Requirements.html)) and against
the topics actually covered in workshops 1–6 of this repo.

**Revision 6** — **R-25 accepted (demo shot list). No findings open.** Q-5, Q-8 and
Q-9 answered; Phase 0 reaches **41 / 42 (97.6%)**, the ceiling with fabrication
deliberately unspent.
[`README.md`](./README.md) matches.

**How to use this document.** Every point has a stable ID (`R-nn` findings,
`F-nn` feature proposals, `S-nn` scope decisions, `Q-nn` questions).

- **`✅ RESOLVED`** — your decision and what it changed. Nothing to do unless I
  applied it in a way you did not intend.
- **`⏳ OPEN`** — empty `Decision:` block, still needs an answer.
- **`🆕 NEW`** — consequences of your round-2 answers.

---

## 0. Where the plan stands

### Phase 0 score: 41 / 42 = **97.6%**

| | Weighted | % |
|---|---|---|
| Plan as originally written | 12 / 42 | 29% |
| After round 1 | 25.5 / 42 | 61% |
| After round 2 | 33.5 / 42 | 79.8% — *under* |
| After `AIM` board routed in Phase 0 (R-26) | 36.5 / 42 | 86.9% |
| After R-13 accepted | 39 / 42 | 92.9% |
| **After R-25 accepted (demo shot list)** | **41 / 42** | **97.6%** |
| Pass bar | 33.6 / 42 | 80% |

Deferring `PILOT` to Phase 1 cost nothing, on one condition — see the DMA note under
**Q-14** below.

Per block, Phase 0 only:

| Block | Max | Phase 0 | What moved |
|-------|-----|---------|------------|
| 1. Architecture | 5 | **5** ↑ | R-12 + R-16 accepted — FSM and diagram now in |
| 2. Peripherals | 5 | **5** ↑ | 2.5 gets stated via R-15 |
| 3. RTOS | 4 | **4** ↑ | 3.4 write-up lands with the docs |
| 4. Control | 4 | 4 | — |
| 5. Reliability | 4 | **4** ↑↑ | **R-13 accepted** — +2.5 |
| 6. Performance | 4 | **4** | R-15 accepted. 6.2 now rests on SD SPI DMA — **Q-14** |
| 7. PCB | 7 | **6** ↑↑ | **R-26 accepted — `AIM` board routed in Phase 0.** Only 7.7 (fabrication) missing |
| 8. Documentation | 5 | **5** | R-16 accepted, +4 |
| 9. Demonstration | 4 | **4** ↑↑ | **R-25 accepted** — ten-shot list in `TASKS.md` §11 |
| **Total** | **42** | **41** | **97.6%** |

With R-25 accepted **no partials are left**, so the strict, midpoint and generous
readings all land on the same number: **41 / 42 = 97.6%**. The single `N` is 7.7
(board fabricated), deliberately unspent — so 41 / 42 is the ceiling and there are
no levers left to pull. Everything from here is execution, not planning.

### The remaining risk is schedule, not scope

Phase 0 now ends with two large last tasks: the `AIM` layout (3 items) and the
reliability hardening (2.5 items). **That is 5.5 items — 13% of the grade — sitting
after a date-gated deadline** whose date is still unset (**Q-7**).

Mitigation already applied to the plan: **F-06, the watchdog, is pulled forward.**
It is an evening's work, independent of everything else, and worth a full item on
its own. Only the parts that genuinely need a running system to harden — F-07
reconnection, F-05 fuzzing, F-19 self-test — stay at the end.

---

## 1. Decisions log

### Round 1

| ID | Decision | Effect |
|----|----------|--------|
| R-01 | accept | Phase 0/1/2 with a hard cut line |
| R-02 | accept | Two boards — schematic, then layout |
| R-03 | accept | FreeRTOS; F-01 adopted |
| R-04 | accept | STM32 = medium only; timestamps on S3; transmission config-gated. **Later made SPI master on the link** — see Round 3 |
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
| **R-13** | **accept** | *"plan it as latest parts of Phase 0"* — **+2.5 items** (5.1, 5.2, 5.4, 9.1). F-06 pulled forward; F-05/F-07/F-19 scheduled last |
| **Q-14** | answered | **`PILOT` firmware → start of Phase 1.** Costs 0 items provided SD SPI DMA covers 6.2; E-stop in Phase 0 is the local `AIM` button |
| **R-25** | **accept** | **Demo shot list accepted** — ten shots in `TASKS.md` §11, each tagged with the requirement it scores. Block 9: 2.5 → **4 / 4**, total **41 / 42 (97.6%)**. Last open finding closed |
| **R-26 + Q-13** | **accept** | **`AIM` board layout moves into Phase 0, scheduled last.** `PILOT` board layout stays Phase 1. **+3.0 items** — 7.2, 7.4, 7.5, 7.6. Phase 0 now passes even on a strict reading |
| **Q-9** | answered | **Local `MODE` button on `AIM` cycles `input.channel`.** Removes the broker as a single point of failure in the config plane; routed through the existing `cmd_q` → validate/apply/persist/ack path, so no second write path and no missed handover reset. Costs one GPIO and ~40 lines in `ui`; supports 5.2 and gives the demo a tenth shot |
| **Q-8** | answered | **No wall clock in Phase 0.** `t_wall_iso` written empty rather than a placeholder epoch; `boot_id` (NVS) orders segments across reboots and `EYE`'s session anchor converts logs to wall time offline. Phase 1 adds SNTP with an `EYE` set-time fallback; **DS3231 declined** — a part, a coin cell and a second I²C device for something `boot_id` already covers. Record schema unchanged between phases |
| `VAULT` bus role | requested | **`VAULT` is SPI master on the `AIM` link**, not slave — `AIM` becomes the slave and `VAULT` pulls records when the card is ready to take them. Storage timing follows the node that owns the card, and `AIM` stops modelling `VAULT`'s state. Costs a `DRDY` line, always-queued slave DMA buffers on `AIM`, and a slave-side liveness timeout; buys a free status path in the same full-duplex frame. Phase 1, no Phase 0 impact |
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
block in the sheet. **Confirmed in round 3** — see **R-26**: the `AIM` board layout
came back into Phase 0 as its last task, and only the `PILOT` layout stayed behind.

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

### R-25 — The video still has to contain 9.1–9.4 `✅ RESOLVED — accepted` `+2.0`

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
Decision: [x] accept   [ ] reject   [ ] discuss
Notes: shoot to the list rather than improvising
```

**Accepted.** The list lives in [`TASKS.md`](./TASKS.md) §11 as the working shot
list — ten shots now, the nine above plus the broker-down `MODE` button shot that
came out of Q-9. Each carries the requirement IDs it scores, so the recording
session is checked off against the sheet rather than judged afterwards. Block 9
goes to **4 / 4** and Phase 0 to **41 / 42**.

### R-26 — PCB layout phasing `✅ RESOLVED — accepted` `+3.0` `answers Q-13`

> *"move the `AIM` board to phase 0, but as latest tasks"*

**Settled: `AIM` board schematic + layout in Phase 0, scheduled last; `PILOT` board
schematic in Phase 0, its layout in Phase 1.** Block 7 goes from 3/7 to 6/7 and
Phase 0 clears the bar on every reading. The original finding follows.

#### Sequencing it last — fine, with one caveat

Doing it last is the right call on the merits: no fabrication means no lead time, so
the only reason to front-load it is gone, and firmware genuinely does benefit from
going first.

The caveat is arithmetic, not process. **You accepted S-04 #4 — Phase 0 is gated by
a date — and this is now the last item before that gate.** Last means first in the
firing line if anything slips, and it is the largest single block in the sheet:
3 items, 7.1% of the grade, and the difference between 86.9% and 79.8%.

Two cheap protections:

1. **Reserve a dated slot for it** rather than leaving it to whatever time remains.
   Extending the `workshop-5-1` layout is perhaps two evenings, not two weeks — it
   only needs to be *scheduled*, not started early.
2. **Sacrifice the `PILOT` board layout instead** if something has to give. It is
   already Phase 1 and worth 0 additional items, so it is the correct thing to lose.

Also note the knock-on: with a real layout in Phase 0, `DESIGN-REVIEW.md` now has
actual routing decisions to explain — power-domain split, USB D± pair, SD return
path — which is what 9.4 is asking for. Schematic-only decisions would have made
that a thinner answer.

<details>
<summary>Original finding</summary>

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

</details>

```text
Decision: [X] accept   [ ] reject   [ ] discuss
Notes: move the AIM board to Phase 0, but as the latest tasks
```

---

## 4. Open findings

**None open.** R-25 and R-13 are both resolved below.

### R-13 — Reliability `✅ RESOLVED — accepted, scheduled last in Phase 0` `+2.5`

> *"Lets plan it as latest parts of the Phase0"*

Right call on sequencing — most of this work wants a running system to harden, so
doing it early would mean hardening something that is still changing shape.

**One split, the same shape as R-14's:** F-06 (the watchdog) does *not* need a
finished system. It is `esp_task_wdt` with `ctrl` and `safety` subscribed, plus
reset-reason logging and a WDT counter in NVS — an evening, independent of
everything else, worth a full item. It is pulled forward to step 6 in the plan's
task order. The rest stays last:

| Feature | When | Closes |
|---|---|---|
| **F-06** — watchdog, reset-reason logging | **step 6, early** | 5.1 |
| F-07 — `EYE` reconnect: reopen the port, rebuild the DepthAI pipeline, log instead of swallowing | last | 5.2, 9.1 |
| F-05 — `tools/fuzz_link.py` against the framing contract | last | 5.4 |
| F-19 — boot self-test gating `ARMED` | last | reinforces 5.2, 9.2 |

Background, unchanged:

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
Decision: [X] accept   [ ] reject   [ ] discuss
Notes: plan it as latest parts of Phase 0
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

### 5. Надійність — 4 / 4 ↑↑ `R-13 accepted`

| # | Requirement | Rev 4 | Now | Why |
|---|-------------|-------|-----|-----|
| 5.1 | WDT | N | **Y** ↑ | F-06, pulled forward to step 6 |
| 5.2 | Peripheral error handling | P | **Y** ↑ | SD conditions (R-24) + `EYE` reconnect (F-07) |
| 5.3 | Invalid data validated | Y | Y | Framing contract (R-09) |
| 5.4 | No hang on bad input | P | **Y** ↑ | F-05 fuzzer |

### 6. Продуктивність — 4 / 4 ↑↑

| # | Requirement | Rev 2 | Now | Why |
|---|-------------|-------|-----|-----|
| 6.1 | Critical-section timing | N | **Y** ↑ | R-15 — SD write latency in telemetry, `esp_timer` on the PID step |
| 6.2 | DMA where appropriate | Y | **Y** ⚠ | **Now rests on SD SPI DMA** — `PILOT` moved to Phase 1. Verify against your IDF version (**Q-14**) |
| 6.3 | No redundant copies | N | **Y** ↑ | R-15 → F-10 data-path paragraph |
| 6.4 | CPU not overloaded | N | **Y** ↑ | R-15 → run-time stats on the OLED |

### 7. PCB — 6 / 7 ↑↑ `R-26 accepted`

| # | Requirement | Rev 3 | Now | Why |
|---|-------------|-------|-----|-----|
| 7.1 | Schematic | Y | Y | Both boards, Phase 0 |
| 7.2 | 2-layer routed | N | **Y** ↑ | `AIM` board routed in Phase 0 |
| 7.3 | Power filtering | Y | Y | Schematic + layout |
| 7.4 | Power/logic separated | P | **Y** ↑ | Pours and star ground now exist |
| 7.5 | High-speed routing | N | **Y** ↑ | USB D± pair, SD return path |
| 7.6 | Test points | P | **Y** ↑ | Placed, not just symbols |
| 7.7 | Fabricated | N | N | S-04 — not ordering. Settled |

### 8. Документація — 5 / 5 ↑↑

| # | Requirement | Rev 2 | Now | Why |
|---|-------------|-------|-----|-----|
| 8.1 | README with task description | P | **Y** ↑ | R-16 |
| 8.2 | Block diagram | N | **Y** ↑ | R-16 |
| 8.3 | Architecture description | N | **Y** ↑ | R-16 |
| 8.4 | Startup instructions | P | **Y** ↑ | R-16 |
| 8.5 | Interfaces explained | N | **Y** ↑ | R-16 + the README's interface table |

### 9. Демонстрація — 4 / 4 `R-25 accepted`

| # | Requirement | Rev 2 | Now | Why |
|---|-------------|-------|-----|-----|
| 9.1 | Stable ≥ 5 min | P | **Y** ↑ | R-13's F-07 makes an unbroken take reliable; shot 8 is the unbroken take |
| 9.2 | Boundary modes | N | **Y** ↑↑ | Shots 3, 5, 6, 7 and 10 hit the limits deliberately |
| 9.3 | Architecture explained | P | **Y** ↑ | Shots 1, 4 and 9 — narration over the R-16 material |
| 9.4 | PCB decisions explained | P | **Y** ↑ | Shot 9 walks `hardware/aim-board/DESIGN-REVIEW.md` |

**Total: Y = 41 · P = 0 · N = 1 → 41 / 42 = 97.6%.**

The one `N` is 7.7 (board fabricated), deliberately unspent. Nothing else is
outstanding at the plan level — block 9 is now scored on shots that are written
down, and the risk moves from *forgetting one* to *recording them*.

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

### Adopted via R-13 — scheduled last in Phase 0

| ID | Feature | When | Closes |
|----|---------|------|--------|
| F-06 | `esp_task_wdt`, reset-reason logging, WDT counter in NVS | **pulled forward** | 5.1 |
| F-07 | `EYE` reconnect, no swallowed exceptions | last | 5.2, 9.1 |
| F-05 | `tools/fuzz_link.py` | last | 5.4 |
| F-19 | Boot self-test gating `ARMED` | last | 5.2, 9.2 |

### Moved to Phase 1 with `PILOT`

`F-11` deep sleep, GPIO wake, battery sense · `EspNowTransport` · the `radio` task ·
the `PILOT` input channel · remote E-stop (the local `AIM` button covers Phase 0)

### Reshaped by R-25

`F-13` (`TEST` command suite) and `F-18` (soak + rehearsal) collapse into **the
accepted shot list** — R-25's nine shots plus the Q-9 broker-down shot, now
`TASKS.md` §11. Much less work, most of the score.

---

## 7. Phases

### S-01 — Phase 0 `✅ accepted`

PC + ESP32-S3 + SD card + ESP32-C3 joystick. **No STM32.**

- Copy the base into an autonomous tree (R-18)
- FreeRTOS port (F-01), config plane (F-23), exclusive channel selector (F-03)
- UART1 data link + framing contract (F-04, R-08, R-09)
- SD logging with rolling files, health and throughput telemetry (F-22, R-15, R-24)
- FSM + diagram (F-02), performance instrumentation (F-08–F-10), documentation (F-17)
- Laser boot-safe GPIO + interlock, firmware only (F-14a)
- Watchdog (F-06) — pulled forward, an evening, worth a full item
- **Schematics for both boards**
- **Last: route the `AIM` board** (R-26, 3 items) **and the reliability hardening**
  (F-07, F-05, F-19 — 2.5 items). Both need reserved slots, not leftover time

### S-02 — Phase 1

**First: `PILOT` firmware** (F-11, ESP-NOW, deep sleep, joystick ADC) and the
`PILOT` board layout. Then MOSFET laser driver (F-14b) · `VAULT` storage node
(F-16) · fault injection (F-21) · BLE transport · CSRT discussion (R-19).

If the Phase 0 date arrives with work outstanding, **the `PILOT` board layout is the
correct thing to sacrifice** — it is already here and worth 0 additional items.

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
| **Q-7** | **Phase 0 deadline date?** | **Open — top question.** 5.5 items now sit in the two last tasks before this gate |
| **DMA** | Does your ESP-IDF version give the `sdspi` bus a DMA channel at `spi_bus_initialize`? | **Open — verify.** With `PILOT` in Phase 1, requirement 6.2 rests on this. If not, 6.2 waits for Phase 1 (−1 item) |
| Q-3 | Is 6.4 "CPU < 70%" or "shown not overloaded"? | Open — changes what you report |
| Q-11 | Log retention (segment size × count)? | Open |
| ~~Q-2~~ | ~~PCB fabricated?~~ | **Answered — no. 7.7 = N** |
| ~~Q-4~~ | ~~Superloop argument?~~ | **Moot — FreeRTOS** |
| ~~Q-5~~ | ~~Parts on hand?~~ | **Answered — yes.** Joystick, SD module, ESP32-C3 and servos are all in hand; no procurement risk in either phase |
| ~~Q-8~~ | ~~Wall-clock source?~~ | **Answered — none in Phase 0.** Monotonic only, `t_wall_iso` empty; `boot_id` in NVS + a session anchor on `EYE` give ordering and offline wall-time conversion. **Phase 1: SNTP, `EYE` set-time fallback, DS3231 declined** |
| ~~Q-9~~ | ~~Local override when the broker is down?~~ | **Answered — `MODE` button on the `AIM` board**, short press = next channel, long press = `NONE`. Goes through the same `cmd_q` config path; `config/set` non-retained. Strengthens 5.2 |
| ~~Q-10~~ | ~~Which board if only one?~~ | **Answered — both schematics** |
| ~~Q-12~~ | ~~C3 in Phase 0?~~ | Superseded by **Q-14** |
| ~~Q-13~~ | ~~PCB phasing?~~ | **Answered — `AIM` layout in Phase 0, last; `PILOT` layout Phase 1** |
| ~~Q-14~~ | ~~`PILOT` firmware in Phase 0?~~ | **Answered — no, start of Phase 1** |

---

## 9. Summary

**Phase 0 scores 41 / 42 = 97.6%** — and with no partials left, that is the number
on every reading, strict or generous. The single missing item is 7.7 (board
fabricated), deliberately unspent, so this is the ceiling.

**No findings open.** R-25 was the last, closed by accepting the demo shot list now
sitting in `TASKS.md` §11.

**The remaining risk is schedule, not scope.** Phase 0 ends with two large tasks —
the `AIM` board layout (3 items) and the reliability hardening (2.5 items). 13% of
the grade sits after a date-gated deadline whose date is still unset (**Q-7**). The
watchdog has been pulled forward to soften that; the rest genuinely benefits from a
running system to harden.

**One thing to verify rather than assume:** with `PILOT` in Phase 1, requirement 6.2
(DMA) rests entirely on the SD SPI path. ESP-IDF's `sdspi` driver takes a DMA
channel at `spi_bus_initialize` in the standard example — confirm it holds in your
IDF version. If it does not, 6.2 waits for `PILOT`'s `adc_continuous` in Phase 1 and
Phase 0 is 38 / 42 (90.5%), still well clear.

**Task list:** [`TASKS.md`](./TASKS.md).
