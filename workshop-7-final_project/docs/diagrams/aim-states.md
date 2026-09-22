# AIM states

The `ctrl` task FSM (`ctrl` is the sole owner; `trigger` names the token
logged with each transition). Source:
[`StateMachine.hpp`](../../firmware/aim/src/StateMachine.hpp),
[`Ctrl.hpp`](../../firmware/aim/src/tasks/Ctrl.hpp).

```mermaid
stateDiagram-v2
    [*] --> BOOT
    BOOT --> SELFTEST: boot

    SELFTEST --> FAULT: selftest.fail
    SELFTEST --> DISARMED: selftest.ok
    SELFTEST --> ZONE_TOUR: selftest.ok (boot.tour on)

    ZONE_TOUR --> DISARMED: tour.done
    DISARMED --> ZONE_TOUR: cfg.tour
    PARKED --> ZONE_TOUR: cfg.tour

    DISARMED --> ARMED: btn.control
    ARMED --> DISARMED: btn.control

    DISARMED --> PARKED: idle
    PARKED --> DISARMED: cfg.channel

    ARMED --> PARKED: idle
    ARMED --> LINK_LOST: link.stale
    LINK_LOST --> ARMED: link.fresh
    LINK_LOST --> DISARMED: btn.control

    FAULT --> DISARMED: fault.ack

    BOOT --> FAULT: estop
    SELFTEST --> FAULT: estop
    ZONE_TOUR --> FAULT: estop
    DISARMED --> FAULT: estop
    ARMED --> FAULT: estop
    PARKED --> FAULT: estop
    LINK_LOST --> FAULT: estop
```

## State legend

| State       | Meaning                                                                          |
| ----------- | --------------------------------------------------------------------------------- |
| `BOOT`      | Power-on, before `SELFTEST` runs. No laser, no motion.                           |
| `SELFTEST`  | Hardware gate — I2C scan, SD mount, servo sweep, camera handshake. Fail goes to `FAULT`. |
| `ZONE_TOUR` | Geometry sweep. Laser lit (no link needed). After `SELFTEST` if `boot.tour` is on, or on demand. Then `DISARMED`. |
| `DISARMED`  | Idle, not driving the gimbal. Laser off. Waits for `btn.control` (arm) or a channel select. |
| `ARMED`     | Closed-loop tracking live. Laser permitted. Motion driven by the selected input channel. |
| `PARKED`    | Idle timeout (30 s) from `DISARMED`/`ARMED` with no channel selected. Servos detached, laser off. |
| `LINK_LOST` | Selected channel stale for over 300 ms. Gimbal stopped, PIDs reset, laser off.    |
| `FAULT`     | Latched by E-stop or self-test failure. Laser forced off; only `fault.ack` clears it. |

## Signal legend

`trigger` tokens logged with each transition (see
[`StateMachine::set`](../../firmware/aim/src/StateMachine.hpp)).

| Trigger         | Meaning                                                              |
| --------------- | --------------------------------------------------------------------- |
| `boot`          | Power-on complete, entering `SELFTEST`.                              |
| `selftest.fail` | Self-test check failed (I2C scan, SD mount, servo sweep, camera handshake). |
| `selftest.ok`   | Self-test passed; goes to `ZONE_TOUR` if `boot.tour` is on, else `DISARMED`. |
| `tour.done`     | Zone-tour sweep finished.                                            |
| `cfg.tour`      | Zone tour requested on demand (from `DISARMED` or `PARKED`).         |
| `btn.control`   | Arm/disarm button pressed.                                           |
| `cfg.channel`   | An input channel was selected.                                       |
| `idle`          | Idle timeout (30 s) with no channel selected.                        |
| `link.stale`    | Selected channel's link went stale (over 300 ms with no data).       |
| `link.fresh`    | Selected channel's link recovered.                                   |
| `estop`         | E-stop asserted; preempts every state except `FAULT` itself.         |
| `fault.ack`     | Operator acknowledged the fault, clearing the latch.                 |

Notes:

- The laser is permitted only in `ZONE_TOUR` and `ARMED`
  ([`stateAllowsLaser`](../../firmware/aim/src/StateMachine.hpp)); it is
  forced off in every other state.
- `estop` preempts every state except `FAULT` itself — it is checked once per
  20 ms control step, ahead of the rest of the FSM.
- `ARM`/`btn.control` is ignored in `BOOT`, `SELFTEST`, `ZONE_TOUR` and
  `FAULT`.
