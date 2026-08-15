# AGENTS.md

Working notes for coding agents. [`README.md`](README.md) is the project
document — task, parts, wiring, commands, safety, TODO — and is not repeated
here. This file covers what you only learn by reading several files at once.

## Repo shape

The git root is `workshops/`, a monorepo of ~30 unrelated workshop projects.
This project is `workshop-5-miniproject/`; **all paths and commands here are
relative to it**, and nothing outside it is in scope unless asked. Other
projects carry their own `CLAUDE.md`.

Two halves live side by side and are built with different toolchains:

|           |                   |                                             |
|-----------|-------------------|---------------------------------------------|
| `src/`    | ESP32-S3 firmware | PlatformIO + **ESP-IDF** (not Arduino), C++ |
| `camera/` | PC vision host    | Python 3, OpenCV + DepthAI + pyserial       |

They share no code. Their only coupling is the wire format in
[`docs/uart-protocol.md`](docs/uart-protocol.md).

## Commands

See the *Commands* section of the README for the `make` targets. Beyond it:

- **There is no test suite.** `test/` is empty and PlatformIO's test runner is
  not configured. Verify firmware changes by building; verify Python changes by
  running `detect_dots.py` against a still image (`--source shot.jpg`), which
  needs no camera and no board.
- **Neither `make` vision target runs bare.** `make vision` exits with
  `SystemExit` when USB VID/PID matches no board; `make vision-dry` defaults to
  `--source cam` and needs the OAK. With neither attached, run
  `py -3 camera/detect_dots.py --source <image>` — omitting `--port` entirely
  is what puts it in detect-and-display mode.
- **Two Python environments exist.** `camera/.venv` has OpenCV **5.0.0**; the
  Makefile's `py -3` has **4.13.0**. Both satisfy `camera/requirements.txt`.
  Prefer the venv when behaviour might be version-sensitive, and say which one
  you used when reporting results.
- `make check-md` is read-only and must pass after any markdown edit; `make
  docs` fixes formatting. Tables in this repo are column-aligned by
  `tools/prettify-md.mjs`, so hand-written tables will fail the check until
  formatted.

## Changing the wire format

The protocol is defined in **three places that must move together**:

1. [`docs/uart-protocol.md`](docs/uart-protocol.md) — the normative contract;
2. `src/inputs/Protocol.hpp` — decodes a line into a `Frame`, no hardware;
3. `camera/serial_link.py` — the PC encoder.

Treat the document as the source of truth and update it first. Two constraints
are load-bearing rather than stylistic:

- **The firmware console shares UART0** with the error stream, so `ESP_LOGI`
  output arrives on the PC's RX. Anything unrecognised must be ignored, never
  treated as an error.
- **`F` must be alone on its line.** A prefix match would let log text fire the
  laser. Keep the exact-line check.

## Firmware architecture

`src/` is **header-only** apart from `main.cpp`. `src/CMakeLists.txt` puts
`drivers`, `parts`, `inputs` and `utils` on `INCLUDE_DIRS`, so headers include
each other by basename (`#include "PWM.hpp"`, not `"drivers/PWM.hpp"`). Keep
that convention when adding files; a new folder needs adding to `INCLUDE_DIRS`.

The layering is an ownership chain, and each layer only knows the one below:

```text
main.cpp            device.tick() then controller.tick(), every LOOP_PERIOD_MS
DeviceController    the rate-limited control step; turns Commands into actions
Device              owns every driver and part, plus the one Input source
parts/              Gimbal, Laser, Beeper - device behaviour, no registers
drivers/            PWM, Servo, Relay, Buzzer, Button, StatusLed, Uart
utils/              Pid, Ema, Queue, Point, ViewPort - no hardware, no IDF
```

Two things this makes non-obvious:

- **The `Gimbal` is the integrator of the control loop.** PIDs output a
  *velocity*; `Gimbal::setVelocity()` integrates it into servo angles. It is
  also stepped every cycle whether or not a frame arrived, which is what keeps
  motion smooth at 50 Hz while frames land at 15–30 Hz.
- **Inputs are decoupled by `Command`.** `ErrorVectorInput` is the only
  `Input` today, but `DeviceController` only ever sees a `Command`, so a second
  source does not touch the controller.

### `Config.hpp` is the tuning surface

Every gain, limit, timing and geometry flag lives there — nothing else should
hold a magic number. It also holds the reasoning (read the gain-budget comment
before touching gains) and a `static_assert` enforcing the invariant that the
**PID output clamp must stay ≤ the Gimbal's hard rate ceiling**; violate it and
anti-windup silently stops working. The README's *Safety* section explains why.

## Camera architecture

The README's *Code layout* lists the modules. The invariants that span them:

- **Detection works in original camera coordinates, always.** `--rotate` is a
  display-only transform. So `overlay.py` draws *before* the rotation, while
  `fire_button.py` draws *after* it — its clicks arrive in window coordinates.
  `simulated_target.py` maps display coordinates back through the rotation.
  Anything new that consumes clicks belongs on the same side as the button.
- **HighGUI addresses windows by title string**, so every `imshow` /
  `namedWindow` / `setMouseCallback` for one window must use the same constant
  (`_WIN`, `_MASK_WIN` in `overlay.py`; the two in `tuning.py`).
- **`controls.py` is Tk, and shares the main thread.** It runs on
  `root.update()` from the frame loop, never `mainloop()`, so its callbacks can
  touch the serial link without locking. Keep it that way — a background thread
  there would need a lock around `ErrorLink`.
- **This OpenCV build has no Qt** (`GUI: WIN32UI`). `cv2.createButton` raises;
  trackbars and mouse callbacks work. Painted-on widgets or Tk are the options.
- Frames are sent every cycle, valid or not: the firmware treats 300 ms of
  silence as a dead link and resets the PIDs, whereas `valid=0` merely holds.

## Conventions

- Comments in both halves explain **why**, often with the measurement or the
  failure that motivated the value. Match that register; do not add narration
  that restates the code.
- Prose in docs and comments uses `--` and `-` rather than em dashes in Python
  and C++ sources; markdown files use real em dashes.
- Fixed-width integer types in firmware; `float` for control maths.
- **C++ exceptions and RTTI are disabled** in `sdkconfig` — no `try`/`catch`,
  no `dynamic_cast`, and errors propagate as return values or `esp_err_t`.
  Nothing in `utils/` includes an IDF or FreeRTOS header; keep it that way so
  the control maths stays testable off-target.
