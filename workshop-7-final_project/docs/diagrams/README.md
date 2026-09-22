# Diagrams — Laser Gimbal with Camera Tracking

Visual diagrams for the system, kept here as Mermaid sources — one file per
diagram, extracted from [`../../README.md`](../../README.md) and
[`../architecture.md`](../architecture.md).

| Diagram | Shows |
|---------|-------|
| [Data flow](./data-flow.md) | `EYE` ↔ `AIM` closed loop, sensors and outputs |
| [AIM states](./aim-states.md) | `ctrl` task FSM, state and signal legends |
| [FreeRTOS tasks](./freertos-tasks.md) | `AIM` task set, cores, priorities, what crosses task boundaries |
