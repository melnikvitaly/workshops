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

  Phase 1:  PILOT ──ESP-NOW──► AIM     EYE ──MQTT──► AIM     AIM ──SPI──► VAULT
             joystick, E-stop         config, telemetry      VAULT clocks
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
> **Plan status.** Phase 0 scores **41 / 42 = 97.6%** against
> [`VERIFICATION/Requirements.html`](./VERIFICATION/Requirements.html) — the same on
> every reading, since no ambiguous items are left. The pass bar is 80%. The one
> missing item is a fabricated board, deliberately unspent.

---

## 1. Phases

The project is built in three phases with a hard cut line. **Phase 0 is the graded
project**; nothing from Phase 1 opens until Phase 0 demonstrates end-to-end.

| Phase | Nodes | Contents |
|-------|-------|----------|
| **0 — graded** | EYE (PC), AIM (ESP32-S3) | Tracking loop on FreeRTOS, FSM, SD logging with health telemetry, **UART1 as the only link** — control *and* config plane, performance instrumentation, documentation, the `AIM` schematic, **`AIM` board layout and reliability work (last tasks)** |
| **1 — after Phase 0 demos** | + PILOT (ESP32-C3), + VAULT (STM32) | **`PILOT` firmware, schematic and board layout (first)**, MQTT config plane and WiFi telemetry, log rotation, MOSFET laser driver, `VAULT` storage node, fault-injection console, BLE telemetry, CSRT |
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
| **EYE** | Sees. Detection, error vector, operator console (broker: Phase 1) | PC | 0 |
| **AIM** | Decides and acts. Control loop, laser, storage, gateway | ESP32-S3 | 0 |
| **PILOT** | Human input. Joystick, remote emergency stop | ESP32-C3 | 1 |
| **VAULT** | Remembers. Storage medium only — **SPI master, pulls records** | STM32 | 1 |

The names are also the reason the STM32 could be deferred without a redesign:
`VAULT` is a role, and in Phase 0 that role is played by `AIM` itself.

```text
log tag   chip        MQTT topic (Phase 1)                source directory
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

Schematic first, then layout. **Phase 0 draws and routes one board — `AIM`.**
The `PILOT` board, schematic included, is Phase 1.

### `AIM` board — laser gimbal controller (ESP32-S3) — *the graded board*

ESP32-S3-WROOM-1; USB-C input, BQ24040 Li-Ion charger and TLV758P LDO; servo power
rail; **MOSFET laser driver**; micro-SD on SPI; OLED I²C header; UART1 header;
E-stop, `MODE` and control buttons.

| Requirement | How this board answers it |
|---|---|
| Power filtering | Bulk electrolytic on the servo rail sized for stall current, 10 µF + 100 nF per rail, 100 nF at every IC pin, ferrite between servo rail and logic, RC on analog inputs |
| Power/logic separation | Servos are a noisy inductive 5–6 V load with amp-level stall transients; the 3.3 V logic and the SD card are not. Separate pours, single-point star ground, servo return never shared with SD or ADC ground |
| High-speed routing | USB D± as a 90 Ω differential pair, length-matched, no stubs or vias on the pair; SD SPI kept short with a continuous return path directly beneath it; RF keep-out under the module |
| Test points | 3V3, VSERVO, VBAT, GND ×2, laser gate, SD SCK/MOSI/MISO/CS, UART1 TX/RX — labelled |

### `PILOT` board — wireless remote (ESP32-C3) — *Phase 1*

ESP32-C3-MINI-1, USB-C + charger + LDO, 2-axis joystick with RC filtering, E-stop
button, optional OLED header, optional nRF24 header (Phase 2), battery connector,
test points.


## 4. Demonstration

TBD

---

## Verification

- Requirements: [`VERIFICATION/Requirements.html`](./VERIFICATION/Requirements.html)
  ([source sheet](https://docs.google.com/spreadsheets/d/1Eip9bvWQhd6_9XutWGIf_u8ECNoNm-KQcv1kDBZhbcY/edit?gid=0#gid=0))
- Task checklist with per-item requirement mapping: [`TASKS.md`](./TASKS.md)
- Presentation ([template](https://docs.google.com/presentation/d/1kyIhydXwOALcR5MxjU38RSwIHLpqRsR5ne6F9L6wNbw/edit?slide=id.p1#slide=id.p1))
- Video demonstration
