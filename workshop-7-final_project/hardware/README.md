# Hardware

KiCad projects for the boards, one directory per board.

| Directory     | Board                          | Phase | Status                     |
|---------------|--------------------------------|-------|----------------------------|
| `aim-board/`  | ESP32-S3 gimbal controller     | 0     | schematic + layout, tasks 8–9 |
| `pilot-board/`| ESP32-C3 wireless remote       | 1     | not started                |

The `aim-board/` design extends the power section from `workshop-5-1` (USB-C +
BQ24040 + TLV758P), **copied in**, not referenced. See
[`../TASKS.md`](../TASKS.md) sections 8 and 9 for the deliverables and
[`../docs/interfaces.md`](../docs/interfaces.md) for the pin map.
