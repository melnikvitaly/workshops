# Final Project — Laser Gimbal with Camera Tracking

A laser on a 2-axis gimbal that closes its control loop **through a camera**. A PC
watches both the laser dot and the target, computes the error vector between them,
and streams it to an ESP32-S3. The firmware runs one PID per axis and drives the
servos **by velocity**, not position. This is image-based visual servoing — the same
structure a camera gimbal tracker uses.

The gimbal can also be driven by hand from the PC, and every control step is
logged to an SD card.

| File | Contents |
|------|----------|
| [`docs/nodes.md`](./docs/nodes.md) | Node roster and per-board hardware |
| [`docs/diagrams.md`](./docs/diagrams.md) | Data flow and other visuals |
| [`docs/architecture.md`](./docs/architecture.md) | Task set, link contracts, wire formats, storage, control and safety |
| [`docs/interfaces.md`](./docs/interfaces.md) | The authoritative pin map, plus every bus with its speed, its rationale and its failure behaviour |
| [`docs/protocol.md`](./docs/protocol.md) | The wire contract both ends implement: control ASCII, NDJSON, CRC-8 and the config plane |
| [`docs/coding.md`](./docs/coding.md) | The firmware rules |

---

## Demonstration

TODO: attach video

---
