# Mini Project 5 — Closed-loop laser tracking (PID)

## Task

A laser on a 2-axis gimbal that **closes the loop through a camera**. A PC
watches both the laser dot and the target, computes the error vector between
them, and streams it to the ESP32 over UART. The firmware runs one PID per axis
and drives the servos by **velocity**, not position.

This is image-based visual servoing — the same structure a camera gimbal
tracker uses.

```text
  camera ──► PC vision ──► error vector ──UART──► ESP32
                 ▲                                  │
                 │                              PID per axis
                 │                                  │
                 │                            velocity (deg/s)
                 │                                  │
                 │                          Gimbal integrates ──► servos
                 │                                  │
                 └────────── laser dot moves ◄──────┘
```

## Demo

[Video](https://drive.google.com/file/d/1_3Ry7DvBVp7bnQGnhnrT0xQXfL38_q39/view?usp=drive_link)

![A tracking session: the gain-preset window on the left, the annotated camera
view on the right — red laser dot, green target, the error vector between them,
and the telemetry echoed back from the ESP32](images/tracking-session.png)

The controls window (left) is `camera/controls.py`; the view on the right is
what `detect_dots.py` draws — the red circle is the laser dot, the green one the
target, and the white line is the error vector being streamed to the board. The
third status line is telemetry coming *back* over the same UART.

## Why this is a real PID

Driving a gimbal open-loop means telling the servo an angle and assuming it is
there. `Servo::angle()` returns the last value *written*, so feeding it back is
algebra, not feedback — nothing physical could ever be corrected.

Here the measurement comes from a camera and is **independent of what was
commanded**. That is what makes it feedback, and it is what lets the loop
reject real disturbances: gravity droop on the tilt axis, servo deadband,
backlash, a horn that slipped on its spline. Push the gimbal with a finger and
it comes back.

Two structural consequences worth knowing:

- **The plant is an integrator.** Servo velocity → angle → dot position is
  `P(s) = k/s`. Commanding velocity rather than position is what makes it that,
  and it is the friendliest plant shape there is.
- **Dead time is the binding constraint.** Camera → vision → UART is
  50–250 ms, and that, not the mechanics, sets the gain ceiling. See the
  tuning note in `Config.hpp`.

## Parts

- 2× SG90 servos on a [pan/tilt gimbal](https://www.printables.com/model/1042622-cheap-fpv-gimbal-pantilt)
- 1× [5 mW 650 nm laser](https://uamper.com/%D0%9B%D0%B0%D0%B7%D0%B5%D1%80-5%D0%BC%D0%92%D1%82-650%D0%BD%D0%BC-%D1%82%D0%BE%D1%87%D0%BA%D0%B0), switched via relay
- Passive buzzer, onboard WS2812 status LED, 2 buttons
- ESP32-S3 DevKitC-1; servos powered from an external supply
- A PC with a camera running the vision pipeline — [`camera/`](camera/README.md),
  in this repo. An OAK-1 by default, but any webcam works (`--source 0`)

There is no potentiometer, encoder rotation or joystick — the PC is the only
motion input now.

## Wiring

See `src/Pinout.hpp`.
The error stream arrives on the **existing USB "UART" connector** (CP2102,
UART0) — no extra wiring at all. GPIO 8, 10 and 18 are now free.

## UART contract

**[`docs/uart-protocol.md`](docs/uart-protocol.md)** — the complete contract
between the two sides. [`camera/`](camera/README.md) is the PC end of it, written
against that document. Format in one line:

```text
E <dx> <dy> <valid>\n        tracking error, streamed   e.g.  E -0.124 0.058 1
F\n                          fire one shot (beam blanks briefly), on demand

A <ex> <ey>\n                <- uplink: arrived on target, once per arrival
```

`dx`/`dy` are `target - dot`, normalised so ±1.0 spans half the frame.

`F` must be alone on its line — `F 1`, `FIRE` and anything else are rejected
rather than fired. That check is load-bearing, not pedantry: the console shares
this UART, so log text arrives on the same wire, and no line that merely starts
with an F may be able to trigger a shot. Fire is enqueued the instant it is
parsed rather than latched into a flag, so two shots arriving inside one 20 ms
control step stay two shots. It works whether or not the loop is armed.

Note the console shares this UART, so log output comes back on the same port
and the PC sender must ignore unrecognised text.

## Controls

| Control               | Action                                               |
|-----------------------|------------------------------------------------------|
| BOOT button           | Arm / disarm the tracking loop                       |
| Encoder button, click | Fire — blank the beam 120 ms, then restore           |
| Encoder button, hold  | Toggle laser constant on/off                         |
| `F\n` over UART       | Fire — same as a button click, on demand from the PC |

Firing is a **gap, not a pulse**. The laser is held constant-on so the camera
can see the dot, so a shot that switched the laser *on* would be invisible;
instead the beam blanks briefly and comes back. The gap is kept well under the
link timeout so a shot never costs the PID its integral.

The laser latches **on at boot** — the camera can only measure the error while
the dot is visible.

## Boot zone tour

At startup, before the loop goes live, the lit laser walks the perimeter of the
working zone **clockwise from the top-left corner** — pausing briefly on each
corner, then parking in the centre. The zone is two pairs of numbers in
`Config.hpp`; this makes it something you can see.

**The direction is the test.** If the dot traces counter-clockwise, one of the
axis-geometry flags is wrong — and that same wrong flag is what sends the
tracking loop running *away* from the target instead of toward it. Reading it
off a 4-second trace is a great deal cheaper than discovering it as a runaway
with the loop live.

```text
TILT_ANGLE_AIMS_DOWN    dot moves down when the tilt angle increases
PAN_ANGLE_AIMS_RIGHT    dot moves right when the pan angle increases
```

These describe how the horns are mounted, not preferences. Which corner the
trace *starts* from tells you which flag is wrong: a mirrored horizontal means
`PAN_ANGLE_AIMS_RIGHT`, a mirrored vertical means `TILT_ANGLE_AIMS_DOWN`.

Roughly 4 s at the default 40 °/s. Disable with `ZONE_TOUR_AT_BOOT = false`.

Buttons and `F` frames work during the tour; motion commands are discarded and
the PIDs stay idle, then get a clean reset at handover so the first control step
measures one step rather than the whole tour.

| Status LED | Meaning                                  |
|------------|------------------------------------------|
| Violet     | Boot zone tour running                   |
| Green      | Armed, tracking                          |
| Amber      | Armed, but no link or target not visible |
| Blue       | Disarmed                                 |

## Commands

A `Makefile` wraps the three toolchains this project uses. `make` on its own
lists everything.

```text
make build           compile the firmware (PlatformIO, esp32-s3-devkitc-1)
make flash           upload, then open the serial monitor at 115200
make flash PORT=COM10   ... on a named port instead of the auto-detected one
make clean           remove build output

make vision          run the PC tracker and stream to the board
make vision-dry      detect and display only, send nothing — the safe way to tune

make deps            install the Python and Node dependencies
make docs            prettify the markdown tables, then lint
```

Docs tooling is [`markdownlint-cli2`](https://github.com/DavidAnson/markdownlint-cli2)
(rules in `.markdownlint-cli2.jsonc`) plus
[`markdown-table-prettify`](https://github.com/darkriszty/MarkdownTablePrettify-VSCodeExt),
driven by `tools/prettify-md.mjs` because its CLI is stdin-only. `make check-md`
is the read-only form: it fails if anything is unformatted or unlinted. Both
have VS Code extensions, recommended in `.vscode/extensions.json`.

## Code layout

Both ends of the loop live here — the firmware in `src/`, the vision in
`camera/`. They meet only at `docs/uart-protocol.md`.

```text
src/                       firmware (ESP-IDF / PlatformIO)
  utils/Pid.hpp            PID: error in, rate (deg/s) out. Anti-windup, filtered D.
  drivers/Uart.hpp         Non-blocking line reader; coexists with the console on UART0.
  inputs/Protocol.hpp      The wire format: decodes one line into a Frame. No hardware.
  inputs/ErrorVectorInput  Consumes decoded frames, runs both PIDs, emits velocity.
  parts/Gimbal.hpp         Integrates commanded velocity into servo angles, clamped.
  DeviceController.hpp     The 50 Hz control step.
  TrackState.hpp           Loop state -> status LED + log.

camera/                    the PC side (Python, see camera/README.md)
  detect_dots.py           Frame sources, the main loop, the command line.
  dots.py                  The detection: red dot, black dots, target choice, error.
  serial_link.py           The COM link and the wire format; standalone sender too.
  overlay.py               What is drawn on each frame; the mask windows.
  fire_button.py           The on-screen FIRE button and its border states.
  controls.py              Gain presets, nudge, telemetry, query -- the Tk window.
  tuning.py                The --debug threshold sliders, and printing them back.
  simulated_target.py      Click or arrow-key a target when no black dot is printed.
```

The PIDs run **on frame arrival**, with `dt` measured between frames — not once
per 20 ms tick. Ticking them against a stale error would integrate the same
value repeatedly and show the derivative a false zero between frames. The
gimbal holds the last commanded rate in between, which is what keeps motion
smooth at 50 Hz while frames land at 15–30 Hz.

## Experimenting with gains

*Gains* and *coefficients* are two words for the same three numbers — `Kp`, `Ki`,
`Kd`. This repo says gains, course material usually says coefficients; see the
terminology table in [`docs/pid-experiments.md`](docs/pid-experiments.md).

Gains are settable at runtime over the same UART, so trying a combination is one
typed line instead of a reflash:

```text
K b 40 4 0     set both axes: Kp=40, Ki=4, Kd=0   (K p / K t for one axis)
N 8 0          nudge pan 8 degrees open-loop -> a repeatable step disturbance
T 1            telemetry stream on (T 0 off)
Q              print current gains
```

`N` is what makes the comparison meaningful: it displaces the gimbal by a known
amount without telling the controller, so the loop sees a pure disturbance that
is identical every run. Hand-moving the target cannot give you that.

`detect_dots.py` puts all four of those on buttons in a second window, plus a
grid of gain presets — one term at a time, a PD ladder, and a deliberately
unstable one — so a comparison is a click while the loop keeps running.

**[`docs/pid-experiments.md`](docs/pid-experiments.md)** walks a sequence of six
gain sets and what each one should look like. Gains revert to the `Config.hpp`
values on reboot.

## Tuning

Read the gain-budget comment in `Config.hpp` first. Order:

1. **Measure your latency `T`** — step the velocity, time until the reported
   error starts changing. This sets everything else.
2. **Measure the plant gain `k`** — command a known 10° step, see what fraction
   of the frame the dot crosses, divide. Repeat at a few points; if `k` drifts,
   that is the mechanical nonlinearity and one fixed gain won't suit everywhere.
3. **P only.** Raise `PAN_KP` until tracking is snappy. Ceiling:
   `Kp·k ≤ 0.5/T`. Shipped defaults sit well under that.
4. **Add I** to kill steady-state error — this is where gravity droop on tilt
   gets cancelled. Watch for windup at the travel limits.
5. **Leave D at zero** unless the error stream is clean; differentiating camera
   noise mostly amplifies it. `PID_DERIV_ALPHA` filters it if you do.

Tune the axes separately — tilt lifts the laser against gravity, pan doesn't.

The log line is plottable:

```text
ex:-0.124 ey:+0.058 vpan:-4.9 vtilt:+2.1 pan:57.4 tilt:88.2 st:TRACK
```

`ex`/`ey` are the process variable (the setpoint is always zero). Watch `ex`
settle after a step to judge overshoot and settling time.

## Safety

- No valid frame for 300 ms → both axes stop and the PIDs reset. A crashed
  PC-side script stops the gimbal instead of letting it coast on a stale error.
- Travel is clamped to the mechanical limits ∩ the working zone, so a sign
  error or runaway cannot drive the arm into its stop. The working zone
  (`WORK_PAN_MIN/MAX`, `WORK_TILT_MIN/MAX` in `Config.hpp`) is deliberately much
  smaller than the full travel — 60° of pan centred on 75°, 30° of tilt centred
  on 100°, against a travel of 90° × 79°. Pointing a
  laser under closed-loop control is exactly where you want the reachable area
  bounded by something other than the mechanics: a bad gain or a confused
  detector should run the dot into a soft edge inside the scene, not sweep it
  across the room. Widen it once the loop is tuned and the directions confirmed.
- **Rotation speed is clamped at two levels.** `SERVO_PAN_MAX_RATE` /
  `SERVO_TILT_MAX_RATE` are a hard ceiling enforced by `Gimbal::setVelocity()`
  on every velocity it is handed, so a mistuned PID or a future input source
  cannot get around it. `PAN_MAX_SLEW` / `TILT_MAX_SLEW` are the PID's own
  output clamp — a tuning knob, and what the anti-windup logic treats as
  "saturated".

  Keep the PID clamp **at or below** the hard ceiling. If it were higher, the
  PID would believe it was still in range while the Gimbal was quietly limiting
  the rate, and conditional integration would keep accumulating against a limit
  it cannot observe — exactly the windup the anti-windup logic exists to
  prevent. A `static_assert` in `Config.hpp` enforces this at compile time.
- **Check axis directions at low gain before turning `Kp` up.** A flipped sign
  is positive feedback and the gimbal runs straight to the limit. `PAN_INVERT` /
  `TILT_INVERT` in `Config.hpp`. The bring-up checklist in the protocol doc
  walks through it.

## TODO

### Detection and tracking (PC side)

- **Make the OpenCV detection less fragile.** It is the weakest link in the
  loop: `find_black_dots` thresholds on darkness and saturation relative to the
  surrounding paper and on six independent shape measurements, so a shadow
  across the sheet, a glossy print, a steep viewing angle or a cluttered
  background can still lose the dot. (Inventing one is much harder than it was —
  the shape gate rejects squares, ellipses, rings and text — but a steep enough
  angle turns the real dot into an ellipse and loses it in turn.)
  Losing it mid-move is not cosmetic — `valid=0` holds the axes,
  and 300 ms of it resets the PIDs. Worth having: a confidence score instead of
  a boolean, hysteresis so a target that was locked is not dropped on one bad
  frame, and a short coast across dropouts rather than an immediate stop.
  Concretely, `--black-offset` is the parameter that has to be retuned by hand
  per lighting setup: the default 12 misses dots under some light, but raising
  it far enough to catch them (60 was tried) is not an improvement either — it
  trades missed dots for false ones. A single fixed offset is the wrong shape of
  knob; it wants to be derived from the frame (local contrast / Otsu on the
  paper region) rather than set on the command line.
- **Integrate [CSRT-tracker-standalone](https://github.com/4ndr3aR/CSRT-tracker-standalone)
  as an alternative detection module.** This would be a structural change, not
  a swap: what runs now is stateless per-frame *detection*, while CSRT is a
  *tracker* — initialise it once on a box and it follows that object between
  frames, which is exactly what survives the clutter and partial occlusion the
  threshold detector cannot. The costs are the mirror image: it needs an initial
  box (a click, or a first detection), and it can drift onto the background with
  no built-in way to notice, so it needs a periodic re-detect to correct it.
  Note before starting: that repo is C++ against ROS Kinetic and OpenCV
  3.3.1-dev, so wiring it to this Python host is real work. The same algorithm
  ships as `cv2.TrackerCSRT_create()` in `opencv-contrib-python` — not in the
  plain `opencv-python 5.0.0` we install now — which is the cheap way to find
  out whether CSRT is even the right answer here before porting anything.
- **Alternative: run detection on the OAK-1 itself,** a small YOLO on the
  camera's own inference cores rather than on the host. The dots are simple
  enough to be an easy class, and the payoff is latency: dead time is the
  binding constraint on the whole loop (see *Why this is a real PID*), and
  moving inference off the USB round-trip attacks it directly. The DepthAI
  pipeline already in `camera_frames()` is the place it would attach, and there
  is precedent to copy: the separate `final_project` (outside this repo) trains
  a laser-dot net and ships RVC2 blobs that run on this same camera.
- **Improve tracking of a moving object.** Everything is tuned for a target
  that holds still. Against a moving one the loop lags by roughly the dead
  time, and the I term only catches up after the fact. Wanted: report target
  velocity from the PC and feed it forward, so the gimbal is commanded to move
  with the target rather than to correct for having been left behind.

### Control and tuning

- **Calibrate properly and re-derive the gains.** The shipped numbers are
  reasoned, not measured. Do the `T` and `k` measurements in *Tuning*, then
  work the preset grid in the controls window against a repeatable `N`
  disturbance and record what actually settles fastest without overshoot.
  Per axis — tilt fights gravity, pan does not.
- **Understand the working zone's shape on the surface, and square the rig to
  it.** The zone is a rectangle in *angles* — `WORK_PAN_MIN/MAX` ×
  `WORK_TILT_MIN/MAX` — and a rectangle in angles is not a rectangle on a wall.
  Two separate effects, worth telling apart before trying to fix either:
  - **Projection (cannot be aligned away).** For a plane square to the centre
    ray, the dot lands at `x = d·tan(pan)`, `y = d·tan(tilt)·sec(pan)`. The
    `sec(pan)` is the problem: the same tilt sweep covers more of the wall the
    further off-centre the pan is. At the configured ±30° of pan that is 15.5%
    taller at the left and right edges than in the middle, so the edges bow out
    — barrel-shaped rather than a true trapezoid. Only a different *definition*
    of the zone removes this: bound it in surface coordinates and convert to
    angles, rather than bounding the angles directly.
  - **Keystone (can be aligned away).** If the surface is not perpendicular to
    the centre ray, the zone skews into a genuine trapezoid — the projector
    problem. The tilt window is centred on 100° and "aims low", so the rig is
    already looking down at the sheet rather than straight at it. Mounting the
    gimbal square to the surface and centred on the zone is the whole fix, and
    it costs nothing but care during setup.

  **Probably not critical, and worth confirming that.** The zone is a safety
  clamp, not a coordinate frame: it only has to keep the beam inside the scene,
  and a bowed edge does that as long as the *worst* corner is still comfortably
  inside. It would start to matter if the zone edges were ever used as a
  reference for anything, or if the boot tour became hard to read as a
  direction check. The real reason to understand it is the next item — the
  `tan`/`sec` non-linearity here and the plant gain `k` varying across travel
  are the same effect, so measuring one explains the other.
- Gain scheduling if `k` turns out to vary much across the travel
- Acceleration limit as well as the rate limit, to soften current spikes on the
  servo supply
- Preserve angles across reboots (NVS)

### Hardware

- **Adjustable laser brightness.** Right now the beam is one fixed level, full
  on or off. Being able to set it would let the dot be matched to the ambient
  light and the camera exposure instead of the room being matched to the laser:
  too bright and it blooms, saturating a patch larger than the real spot and
  dragging the centroid around with it; too dim and it is lost against a lit
  wall. That is the same failure the detection-fragility item is about,
  attacked from the hardware end. Running at the minimum brightness the camera
  can see is also simply safer for a device that sweeps a beam around a room.

  **The blocker is the relay** (`RELAY`, GPIO 6, `drivers/Relay.hpp`). A
  mechanical relay is on/off by construction — it cannot be dimmed at any
  useful rate. This needs a transistor/MOSFET low-side switch in its place, or
  a laser driver with an analog brightness input. Worth doing anyway: that same
  swap is the likely cure for the relay-glitch entry under *Known issues*.

  The firmware side is nearly free once the hardware allows it — `drivers/PWM.hpp`
  is already a generic LEDC channel documented for exactly this kind of load
  (~1 kHz, coarse duty), so `parts/Laser.hpp` would take a `PWM` instead of a
  `Relay`. Two things to get right: firing is a **gap**, so the blank must
  restore the *set* brightness rather than full on; and PWM interacts with the
  camera — keep the carrier well above the frame rate, and expect that with a
  short exposure the dot can still alias into flickering frame to frame at a
  constant duty. Analog current control side-steps that entirely.

  Then expose it: a brightness letter alongside `K`/`N`/`T`/`Q` in the UART
  protocol, and a slider in the controls window next to the gain presets.
- **More precise mechanics.** SG90s have visible backlash and deadband, which
  put a floor under how tightly the loop can hold the dot no matter how the
  gains are set — past a point, tuning is chasing slop. Metal-gear or digital
  servos, a stiffer gimbal, and a laser mount that cannot shift on its horn
  would each raise that floor.

- **Debug the laser switching itself on when it should be off.** The beam
  sometimes lights without being asked to. This is a safety item, not a
  cosmetic one — a 5 mW emitter on a gimbal that can sweep a room should only
  ever be lit deliberately. Two candidate mechanisms, both worth ruling in or
  out with a scope on GPIO 6 rather than by reading code:
  - **The pin is undriven until firmware gets to it.** The relay module is
    active-low (`RELAY_ACTIVE_HIGH = false`), so *off* means holding GPIO 6
    HIGH — but from power-on through the bootloader until `Relay::init()` runs,
    nothing drives it and no pull is configured, so it floats and the module is
    free to read it as LOW. That is the reset/boot/flash glitch already listed
    under *Known issues*, seen from the laser's end. `init()` has a narrower
    version of the same hole: `gpio_config()` enables the output before `off()`
    writes a level, and the output register comes out of reset at 0 — the pin is
    briefly driven LOW, i.e. energized. Cheap fixes to try: `gpio_set_level()`
    to the inactive level *before* `gpio_config()`, an internal pull-up so the
    floating window rests off, and an external pull-up on the module's input
    line, which is the only one that covers the pre-firmware window.
  - **A spurious long-press toggles the latch on.** `CONTROL_BTN` (GPIO 17)
    long-press maps to `LaserToggleConstant`, so one false hold on a noisy or
    marginally-pulled button line latches the beam on and leaves it on. Check
    the pull and debounce on that input before blaming the relay.

  Note the blank/fire path is not a suspect: `Laser::fire()` only ever forces
  the beam *off*, and `apply()` gates on `_constant`, so nothing in the firing
  logic can light a laser that was dark. The MOSFET swap under *Adjustable laser
  brightness* would likely settle the relay half of this for good.

### Reliability

- **Make it run stably for a long session.** It works for a demo; it has not
  been asked to survive an afternoon. Three concrete gaps, all on the PC side:
  - **Nothing reconnects.** `ErrorLink._write` catches a dead port so a USB
    hiccup cannot kill the vision loop — but nothing ever reopens it, so after
    the ESP32 re-enumerates (reset, unplug, reflash) every frame just prints
    *Serial write failed* and the gimbal sits in failsafe until the script is
    restarted by hand. The camera is worse: if the OAK drops, `camera_frames()`
    simply stops yielding, the `for` loop ends and the app exits.
  - **Errors are swallowed.** The two bare `except Exception: pass` around the
    key handlers in `run()` were there to stop a UI slip from killing the loop,
    but they also discard the traceback, so a real bug in `Controls` or
    `SimulatedTargetManager` presents as a key that silently stops working. Log
    the exception, keep running.
  - **Nobody has measured the drift.** No known leak or slowdown, but no long
    run either — worth leaving it streaming for a few hours and watching fps,
    memory and the sent/fired counters before trusting it unattended.

  Wanted: reopen the port when it reappears, rebuild the DepthAI pipeline on
  camera loss, and keep the loop alive across both instead of exiting.

### Known issues

- Relay switching glitches during reset/boot/flash — control relay power
  separately, and pin down the exact GPIO behaviour
