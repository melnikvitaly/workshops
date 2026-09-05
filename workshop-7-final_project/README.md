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

> **Architecture:** [`docs/architecture.md`](./docs/architecture.md) — the technical
> design: task set, link contracts, wire formats, storage, control and safety.
>
> **Task list:** [`TASKS.md`](./TASKS.md) — the phased checklist extracted from this
> plan, with requirement IDs against each item.
>
> **Plan status.** Phase 0 scores **39 / 42 = 92.9%** against
> [`VERIFICATION/Requirements.html`](./VERIFICATION/Requirements.html) — 90.5% even
> if every ambiguous item is graded against it. The pass bar is 80%.

---

## 1. Phases

The project is built in three phases with a hard cut line. **Phase 0 is the graded
project**; nothing from Phase 1 opens until Phase 0 demonstrates end-to-end.

| Phase | Nodes | Contents |
|-------|-------|----------|
| **0 — graded** | EYE (PC), AIM (ESP32-S3) | Tracking loop on FreeRTOS, FSM, SD logging with health telemetry, UART1 + MQTT to `EYE`, performance instrumentation, documentation, schematics for both boards, **`AIM` board layout and reliability work (last tasks)** |
| **1 — after Phase 0 demos** | + PILOT (ESP32-C3), + VAULT (STM32) | **`PILOT` firmware and board layout (first)**, MOSFET laser driver, `VAULT` storage node, fault-injection console, BLE telemetry, CSRT |
| **2 — cut without regret** | — | NRF24L01+, on-camera YOLO, target-velocity feed-forward, joystick OLED, PWM laser brightness |

**Ordering principle:** firmware first, board layout and reliability hardening last.
No board is being fabricated, so a layout carries no lead time and nothing is gained
by front-loading it; the hardening wants a running system to harden. The exception
is the watchdog, which is independent of everything else and lands early.

The step-by-step breakdown is in [`TASKS.md`](./TASKS.md).

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
| **PILOT** | Human input. Joystick, remote emergency stop | ESP32-C3 | 1 |
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

Per-node responsibilities in detail — what `EYE` detects, what `AIM` owns, what
`PILOT` and `VAULT` are for — are in
[`docs/architecture.md`](./docs/architecture.md#1-node-responsibilities), together
with the task set, the link contracts, the storage and control design and the
safety interlock.

---

## 3. Hardware and PCB

Schematic first, then layout, for **two boards**.

### `AIM` board — laser gimbal controller (ESP32-S3) — *the graded board*

ESP32-S3-WROOM-1; USB-C input, BQ24040 Li-Ion charger and TLV758P LDO; servo power
rail; **MOSFET laser driver**; micro-SD on SPI; OLED I²C header; UART1 header;
E-stop and control buttons.

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

The output set is plots, render, BOM and a clean DRC, plus a `DESIGN-REVIEW.md` per
board explaining the handful of decisions actually made.
**That document is what the PCB half of the demo is built on**, so it argues the
power-domain split, the USB D± pair and the SD return path rather than listing them.

> **Note.** The verification block is satisfied by *one* board done properly, which
> is why the `AIM` board is routed in Phase 0 and the `PILOT` board's layout waits
> for Phase 1. The `PILOT` board's **schematic** is still Phase 0.

---

## 4. Repository layout

The final project is **fully autonomous**. Sources are **copied** in, never
referenced across directories; nothing in the build reaches outside this folder.

```text
workshop-7-final_project/
  README.md              this plan
  TASKS.md               phased checklist
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
  docs/
    architecture.md      system architecture (this plan's technical half)
    …                    interfaces, protocol, bringup
  VERIFICATION/          requirements sheet + coverage tracking
```

---

## 5. Demonstration

The demo is a recorded video rather than a live run, which means it is shot to a
list rather than improvised: the demonstration requirements are scored on what the
video *contains*, not on the fact that a recording exists. Showing the system work
is the easy half — the video also has to show the limits being hit deliberately,
explain the architecture, and walk through the board.

The one that is easy to miss is stability. An edited highlight reel does not
demonstrate it; a single continuous take does, so the cut includes one unbroken
five-minute run with the uptime counter on screen.

The shot list is in [`TASKS.md`](./TASKS.md).

---

## Verification

- Requirements: [`VERIFICATION/Requirements.html`](./VERIFICATION/Requirements.html)
  ([source sheet](https://docs.google.com/spreadsheets/d/1Eip9bvWQhd6_9XutWGIf_u8ECNoNm-KQcv1kDBZhbcY/edit?gid=0#gid=0))
- Task checklist with per-item requirement mapping: [`TASKS.md`](./TASKS.md)
- Presentation ([template](https://docs.google.com/presentation/d/1kyIhydXwOALcR5MxjU38RSwIHLpqRsR5ne6F9L6wNbw/edit?slide=id.p1#slide=id.p1))
- Video demonstration
