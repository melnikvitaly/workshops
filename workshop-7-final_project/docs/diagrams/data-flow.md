# Data flow

Phase 0 closed loop (`EYE` ↔ `AIM`). Source:
[`architecture.md` §1](../architecture.md#1-nodes),
[§3](../architecture.md#3-communications).

```mermaid
flowchart LR
    camera([camera]) -->|video frame| EYE[EYE - PC]

    subgraph AIM[AIM - ESP32-S3]
        direction LR
        subgraph CTRL[one active channel]
            direction TB
            CTRL_POS["AUTO_POSITIONAL<br/>PID per axis"]
            CTRL_VEQ["AUTO_VELOCITYEQUATION<br/>PID per axis"]
        end
        CTRL_POS -->|velocity deg/s, integrated each tick| GIMBAL[Servo driver]
        CTRL_VEQ -->|angle += delta, no integration| GIMBAL
    end

    EYE -->|error vector<br/>UART1| CTRL
    EYE -->|config cfg.set NDJSON<br/>UART1| CTRL
    CTRL -->|telemetry NDJSON<br/>UART1| EYE
    GIMBAL -->|angle command| SERVOS([servos])
    SERVOS -.->|laser dot moves| camera
    CTRL -->|telemetry record| SD([SD card])
    CTRL -->|status text<br/>I2C| OLED[OLED display]

    classDef compute fill:#D0E2F5,stroke:#333,stroke-width:1px;
    classDef external fill:#F2F2F2,stroke:#333,stroke-width:1px;
    class EYE,CTRL_POS,CTRL_VEQ,GIMBAL compute
    class camera,SERVOS,SD,OLED external
    style AIM fill:#EDE7DE,stroke:#999
    style CTRL fill:#F5F1E9,stroke:#999
```

**EYE** (PC) sees · **AIM** (ESP32-S3) decides and acts.

**Exactly one channel is active at a time** ([`architecture.md` §4](../architecture.md#4-input-channels)):
`AUTO_POSITIONAL`'s PID outputs a velocity that `Servo driver` integrates into an
angle every 20 ms tick; `AUTO_VELOCITYEQUATION`'s PID instead outputs an angle
delta added straight onto the current angle, with no separate integration step
— see [`servo-control-strategies.md`](../servo-control-strategies.md). `MANUAL`
and `NONE` (keyboard drive, idle) are omitted here for clarity — same diagram,
different source feeding `GIMBAL`.
