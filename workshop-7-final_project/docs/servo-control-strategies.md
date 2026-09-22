# Servo Control Strategies — Direct Position vs Velocity-Form PID

Two ways to drive a PID output onto a standard hobby servo (an internal
position-tracking actuator). `AIM` uses **velocity-form PID with position
stepping** — this doc explains why, against the alternative. The
implementation is [`utils/Pid.hpp`](../firmware/aim/src/utils/Pid.hpp); the
design rationale is [`architecture.md` §6](./architecture.md#6-control).

## Contents

1. [Direct position control](#1-direct-position-control)
2. [Velocity-form PID (position stepping)](#2-velocity-form-pid-position-stepping)
3. [Comparison](#3-comparison)
4. [References](#4-references)

---

## 1. Direct position control

The PID output **is** the commanded angle, sent straight to the servo's own
internal position loop:

```text
u[k]     = Kp*e[k] + Ki*sum(e[0..k])*dt + Kd*(e[k]-e[k-1])/dt
angle[k] = clamp(u[k], angleMin, angleMax)
```

Every PID sample is a full position setpoint change. Simple, and fast for a
single large step, but every noisy or aggressive sample is a real position
command the servo must chase.

## 2. Velocity-form PID (position stepping)

The PID output is a **rate**; the microcontroller integrates it into the
position command each tick:

```text
v[k]     = Kp*e[k] + Ki*sum(e[0..k])*dt + Kd*(e[k]-e[k-1])/dt
v[k]     = clamp(v[k], rateMin, rateMax)
angle[k] = clamp(angle[k-1] + v[k]*dt, angleMin, angleMax)   // integration step
```

The extra line — `angle[k-1] + v[k]*dt` — has no equivalent in §1: it is what
makes the plant seen by the outer loop `P(s) = k/s`, a pure integrator,
instead of the servo's own near-unity closed position loop. This is exactly
[`Pid::update()`](../firmware/aim/src/utils/Pid.hpp)'s contract — the caller
does `angle += pid.update(error, dt) * dt`.

---

## 3. Comparison

| Dimension | Direct position control | Velocity-form PID (position stepping) |
|---|---|---|
| **Core representation** | PID output = target angle (position domain) | PID output = target rate; MCU integrates to angle each tick |
| **Plant model seen by outer loop** | Near-unity gain with lag — servo's own loop absorbs the step | Pure integrator, `P(s) = k/s` |
| **Steady-state error** | Needs Ki to null static error; a wound-up Ki is a real position offset | Plant integration often nulls it on its own; Ki compensates only residual effects (backlash, load droop); wound-up Ki is a persistent *rate*, so zero error still needs explicit hold logic |
| **Role of Kp** | Maps error to a position offset | Maps error to a slew rate |
| **Role of Ki** | Primary tool for steady-state error | Often small or zero — the integration step already integrates |
| **Role of Kd** | Damps position command; noise → position jitter directly | Damps rate command; noise is smoothed by the `v*dt` integration step |
| **Overshoot / jerk** | Higher — every sample can be a discontinuous position jump | Lower — position change per tick is rate-limited by construction |
| **Dynamic response to a large step** | Faster — uses the servo's full internal bandwidth | Slower to close large error (rate-limited); settles cleaner near target |
| **Sensor / measurement noise** | Transmitted ~directly into commanded position | Attenuated — noisy rate averages toward its mean before it accumulates |
| **Sensitivity to loop `dt` jitter** | `dt` affects only I and D terms | `dt` also sizes the integration step (`v*dt`); timing accuracy matters more |
| **Saturation / anti-windup** | One clamp (position); anti-windup guards the commanded angle | Two clamps (rate + integrated position); anti-windup must see both, or it reasons about the wrong limit |
| **Interaction with servo's internal loop** | Can fight it if updates arrive faster than the servo settles | Steps stay inside the servo's linear slew range — less fighting |
| **MCU cost** | One PID evaluation per tick | Same, plus one multiply-add and one clamp — negligible |
| **Ideal use case** | Large, infrequent setpoint changes; point-to-point positioning | Continuous closed-loop tracking against a moving/noisy error signal |
| **Applicability to this project** | Poor fit — `AIM` tracks a continuously-moving camera error at 50–250 ms dead time (§6); direct position would feed every noisy/stale detection straight into the servo as a position command | **Used.** Matches the vision-tracking plant exactly: [`architecture.md` §6](./architecture.md#6-control) targets `P(s)=k/s` deliberately, rate limits absorb detector noise and dead time, and [`Pid::hold()`](../firmware/aim/src/utils/Pid.hpp) handles the zero-error/non-zero-Ki case this form requires |

---

## 4. References

Derivations and proofs:

- [Åström & Murray, *Feedback Systems*, ch. 11 "PID Control"][astrom] — free
  PDF; §11.4 derives the discrete PID algorithm and integrator windup, §11.5
  covers anti-windup by tracking (back-calculation).
- [apmonitor.com, "Proportional Integral Derivative (PID)"][apm] — BYU
  process-control notes; gives the positional form
  `u_k = u_bias + Kc*e_k + (Kc/τI)*Σe_i*Δt − Kc*τD*(PV_k−PV_{k-1})/Δt`
  alongside the incremental/velocity form
  `u_k = u_{k-1} + Kp(e_k−e_{k-1}) + Ki*e_k*Δt + Kd(e_k−2e_{k-1}+e_{k-2})/Δt`.
- [PID controller — Wikipedia, "Discrete implementation"][wiki] — concise
  side-by-side of the positional and incremental discrete forms.

Anti-windup theory:

- Caparroz, Soltesz, Hägglund & Guzmán,
  ["Anti-Windup in PID Control: Review, Analysis, and New Tuning
  Directions"][antiwindup] (arXiv:2606.01959) — back-calculation vs.
  conditional integration (the scheme
  [`Pid.hpp`](../firmware/aim/src/utils/Pid.hpp) uses) on saturating
  actuators, with tuning rules.
- Lavretsky, ["Integrator Anti-Windup Design for Servo-Controllers with
  Position Constraints"][lavretsky] (arXiv:2504.12207) — anti-windup for
  position-saturated servo controllers via control-barrier-function theory.

Practical / applied:

- [Brett Beauregard, "Improving the Beginner's PID"][beauregard] — widely
  cited series on derivative-on-measurement, anti-windup by clamping, and
  output-rate limiting on embedded PID loops.
- [Control Engineering, "The Velocity of PID"][controleng] — applied case
  for the incremental/velocity PID form on real actuators.
- [National Instruments, "PID Theory Explained"][ni] — practical comparison
  of positional and velocity PID forms.
- [Modern Robotics (Lynch & Park), ch. 11 "Robot Control"][modernrobotics] —
  free textbook; position vs. velocity control of an actuator inside an
  outer feedback loop.

[astrom]: https://www.cds.caltech.edu/~murray/courses/cds101/fa02/caltech/astrom-ch6.pdf
[apm]: https://apmonitor.com/pdc/index.php/Main/ProportionalIntegralDerivative
[wiki]: https://en.wikipedia.org/wiki/PID_controller#Discrete_implementation
[antiwindup]: https://arxiv.org/abs/2606.01959
[lavretsky]: https://arxiv.org/abs/2504.12207
[beauregard]: http://brettbeauregard.com/blog/2011/04/improving-the-beginner%E2%80%99s-pid-introduction/
[controleng]: https://www.controleng.com/the-velocity-of-pid/
[ni]: https://www.ni.com/en/shop/labview/pid-theory-explained.html
[modernrobotics]: http://hades.mech.northwestern.edu/index.php/Modern_Robotics
