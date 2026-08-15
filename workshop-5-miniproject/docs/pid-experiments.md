# PID experiments — feeling what each term does

Gains are settable over UART, so an experiment is one typed line rather than a
reflash. The point of this document is a *repeatable* procedure: comparing gain
sets only means something if the disturbance is identical each time.

## Terminology

**Gains and coefficients are the same thing.** `Kp`, `Ki`, `Kd` are called the
PID *gains* here and in the code, but *coefficients* (Ukr. *коефіцієнти*),
*parameters* and *tuning constants* all name exactly these three numbers.
Textbooks and course material tend to say coefficients; control engineering and
firmware tend to say gains. Nothing changes but the word — if a source says "the
proportional coefficient", that is `PAN_KP`.

They are called gains because each one is a *multiplier*: the controller
measures the error and each term multiplies its share of it, so raising `Kp`
raises how hard the loop pushes back per unit of error, in exactly the sense an
amplifier has gain.

Two words that are **not** interchangeable with each other:

- A *term* is one of the three contributions the controller sums (`P`, `I`, `D`);
  the *gain* is the number that scales it. "Add D" and "raise Kd" mean the same
  in practice, but "the D term" is the `Kd · d(error)/dt` part of the output
  (low-pass filtered here — see `Pid.hpp`), not `Kd` itself.

| In this repo | Also written as |
|---|---|
| gain | coefficient, parameter, tuning constant |
| `Kp` / `PAN_KP` | proportional coefficient, K<sub>p</sub>, P gain |
| `Ki` / `PAN_KI` | integral coefficient, K<sub>i</sub>, I gain |
| `Kd` / `PAN_KD` | derivative coefficient, K<sub>d</sub>, D gain |
| error | deviation, mismatch, e(t) — here `ex`/`ey` |
| setpoint | reference, command, r(t) — here always zero, see below |
| process variable | controlled variable, PV, measurement — here `ex`/`ey` again |

Careful with *target*: in this project it means the black dot on the paper, the
thing the laser is aimed at. It does **not** mean the setpoint, even though
control texts often use it that way.

That is because the setpoint here is zero: the controlled quantity is the *error
vector itself*, and the job is to drive it to (0, 0). So `ex` and `ey` in the
telemetry are the process variable, not a distance from some separate reference
— which is why the setpoint never appears in the wire protocol at all.

## The four commands

```
Q                          print current gains
K b 40 4 0                 set BOTH axes: Kp=40, Ki=4, Kd=0
K p 40 4 0                 pan only        (K t ... for tilt only)
T 1                        telemetry on   (T 0 to silence it)
N 8 0                      nudge pan 8 degrees, open loop
```

`N` is the important one. It displaces the gimbal by a known number of degrees
without telling the controller — so to the loop it looks exactly like the world
moving, i.e. a disturbance to reject. Identical every time, which is what makes
two gain sets comparable. Hand-moving the target cannot give you that.

Gain changes deliberately do **not** reset the integrators, so what you see is
how that gain set behaves from the state you are already in.

## The measurement loop

With the camera running, the laser on target, and the loop armed:

```
T 1                 start the telemetry stream
K b 40 0 0          the gain set under test
N 8 0               kick it
                    ...watch ex return to zero...
N -8 0              kick it back
T 0                 stop the stream
```

Telemetry lines are plottable:

```
ex:-0.124 ey:+0.058 vpan:-4.9 vtilt:+2.1 pan:57.4 tilt:88.2 st:TRACK
```

`ex` is the process variable; the setpoint is always zero. Three things to read
off the recovery:

- **Overshoot** — does `ex` cross zero and come back?
- **Settling time** — how long until it stays inside the deadzone?
- **Steady state** — where does it stop? Nonzero means the loop gave up short.

The `A <ex> <ey>` uplink marks the instant it settles, which times the recovery
without eyeballing the trace.

## The sequence worth running

Start here and change one thing at a time.

| # | Command | What to watch for |
|---|---|---|
| 1 | `K b 10 0 0` | Sluggish. Crawls back, may stop visibly short — P alone gets weaker as the error shrinks. |
| 2 | `K b 40 0 0` | Snappier, still stops short. **This is the shipped P-only behaviour.** |
| 3 | `K b 90 0 0` | Fast, probably overshoots. Push further and it will oscillate — that is the latency limit, not the mechanics. |
| 4 | `K b 40 4 0` | The residual now closes. Slower to *finish*, but it actually arrives. |
| 5 | `K b 40 12 0` | Overshoot appears, possibly a slow hunt around the target. Too much I. |
| 6 | `K b 40 4 2` | D damps the approach — but on camera noise it may just add jitter. |

## What you should conclude

**P sets speed and cannot finish.** Its output shrinks with the error, so near
the target it commands a rate too small to overcome ~1° of servo deadband. That
is why experiment 2 stops short: not a bug, a property.

**I finishes, and costs phase.** It accumulates the residual until the output is
big enough to move the servo. It is also the only term that can cancel a
standing bias like gravity droop on tilt. Too much and you get overshoot, then
hunting — the integral keeps pushing after the error has already reversed.

**D is nearly useless here.** With a pure-integrator plant, P-only cannot
overshoot, so early on D has nothing to damp. Once I makes the system
second-order, D can help — but it differentiates a noisy camera measurement, and
mostly amplifies that noise. `PID_DERIV_ALPHA` filters it. If experiment 6 is
worse than 4, that is the honest result, not a mistake.

**Latency sets the ceiling, not the servos.** The plant is `k/s` with dead time
`T`, giving `Kp·k ≤ 0.5/T` for a comfortable margin. Raising `Kp` past that
oscillates no matter how good the mechanism is. Measure `T` by timing a `N` kick
to the first change in `ex`.

## Comparing the axes

Run the same sequence on tilt (`N 0 8`) and expect it to differ — tilt lifts the
laser against gravity, pan does not. That standing load is exactly what I
compensates, so tilt usually wants more of it. Tune them separately:

```
K p 40 4 0
K t 35 6 0
```

## The floor you cannot tune past

An SG90 has roughly 1° of deadband, ≈ 0.02 in normalised units. No gain set
points better than that. Past it, more `Ki` buys hunting rather than accuracy —
the servo jumps the deadband, overshoots, comes back, jumps again.

If a run oscillates at a steady small amplitude and no gain change helps, you
have found the mechanical floor rather than a tuning problem. Better actuators
(metal-gear digital servos, or gear reduction) are the answer, not more gain.

## Housekeeping

- `T 0` when finished — telemetry shares the UART with the incoming error
  stream, and a live stream competes with the frames you are trying to receive.
- Gains reset to the `Config.hpp` values on reboot. When you find a set worth
  keeping, write it into `PAN_KP` / `PAN_KI` / … so it survives.
- Negative gains are rejected by the parser: they invert the loop, and a
  runaway with a laser on it is not a useful experiment.
