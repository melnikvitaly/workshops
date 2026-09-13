# Nodes — Laser Gimbal with Camera Tracking

Each node has a **role name** that does not mention its silicon — the role is
what the design commits to, the chip in brackets is how it happens to be
hosted. The names are the namespace used throughout the project — log tags,
source directories — so a node can be re-hosted on different hardware
without a rename cascade.

Phase 0 has two nodes.

**Schematic first, then layout.** Phase 0 draws and routes one board —
`AIM`, the graded board.

| Name | Role | Runs on | Board |
|------|------|---------|-------|
| **EYE** | Sees. Detection, error vector, operator console | PC | — (host PC) |
| **AIM** | Decides and acts. Control loop, laser, storage | ESP32-S3 | ESP32-S3-WROOM-1; USB-C input, BQ24040 Li-Ion charger and TLV758P LDO; servo power rail; MOSFET laser driver; micro-SD on SPI; OLED I²C header; UART1 header; E-stop, `MODE` and control buttons |

```text
log tag   chip        source directory
EYE       PC          eye/
AIM       ESP32-S3    firmware/aim/
```

## `AIM` board requirements — *the graded board*

| Requirement | How this board answers it |
|---|---|
| Power filtering | Bulk electrolytic on the servo rail sized for stall current, 10 µF + 100 nF per rail, 100 nF at every IC pin, ferrite between servo rail and logic, RC on analog inputs |
| Power/logic separation | Servos are a noisy inductive 5–6 V load with amp-level stall transients; the 3.3 V logic and the SD card are not. Separate pours, single-point star ground, servo return never shared with SD or ADC ground |
| High-speed routing | USB D± as a 90 Ω differential pair, length-matched, no stubs or vias on the pair; SD SPI kept short with a continuous return path directly beneath it; RF keep-out under the module |
| Test points | 3V3, VSERVO, VBAT, GND ×2, laser gate, SD SCK/MOSI/MISO/CS, UART1 TX/RX — labelled |

Per-node responsibilities in detail — what `EYE` detects, what `AIM` owns —
are in [`architecture.md`](./architecture.md#1-node-responsibilities),
together with the task set, the link contracts, the storage and control
design and the safety interlock.
