# Data flow

Phase 0 closed loop (`EYE` ↔ `AIM`). Source:
[`architecture.md` §1](../architecture.md#1-nodes),
[§3](../architecture.md#3-communications).

```mermaid
flowchart LR
    camera([camera]) -->|video frame| EYE[EYE - PC]

    subgraph AIM[AIM - ESP32-S3]
        direction LR
        CTRL[PID per axis] -->|velocity deg/s| GIMBAL[Servo driver<br/>integrates velocity]
    end

    EYE -->|error vector<br/>UART1| CTRL
    EYE -->|config cfg.set NDJSON<br/>UART1| CTRL
    CTRL -->|telemetry NDJSON<br/>UART1| EYE
    GIMBAL -->|angle command| SERVOS([servos])
    SERVOS -.->|laser dot moves| camera
    CTRL -->|telemetry record| SD([SD card])
    CTRL -->|status text<br/>I2C| OLED[OLED display]
```

**EYE** (PC) sees · **AIM** (ESP32-S3) decides and acts.
