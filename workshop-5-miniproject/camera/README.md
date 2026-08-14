# camera/ — the PC end of the loop

Finds the **red dot** (where the laser points now) and the **black printed dot**
(where it should point) in the OAK camera image, and streams the error between
them to the ESP32 over the COM port.

```
OAK-1 ──USB──> PC: detect_dots.py ──COM──> ESP32-S3 ──> gimbal + laser
                    red dot, black dot        E <dx> <dy> <valid>
                    error = target − laser    F  (fire)
```

Detection runs on the **host** in plain OpenCV — the OAK is used as a camera
only. The camera plumbing (DepthAI v3) mirrors
`final_project/camera-host/detect.py`, so the same pipeline shape carries over;
the red-dot NN of that project is unnecessary here because a PC can run the
color filter directly.

| File | |
|---|---|
| `detect_dots.py` | main script: frames → detection → error vector → COM, with the live view and the FIRE button |
| `dots.py` | the detection itself: red dot, black dots, target choice, error vector |
| `serial_link.py` | the COM link and the wire format; also a standalone sender for bring-up |

## Install and run

```bash
py -3 -m pip install -r requirements.txt

py -3 detect_dots.py --port                # live OAK -> ESP32, port auto-detected
py -3 detect_dots.py --port COM5           # ... or name it
py -3 detect_dots.py                       # live OAK, detection only, nothing sent
py -3 detect_dots.py --source shot.jpg --debug     # tune thresholds on one image
py -3 detect_dots.py --source frames/ --debug      # step through a folder
py -3 detect_dots.py --source 0                    # any USB webcam, no OAK needed
```

Omit `--port` entirely and the script detects and displays but sends nothing —
the safe way to tune.

### Finding the port

Bare `--port` (or `--port auto`) identifies the board by **USB VID/PID**, not by
name: COM numbers are handed out by Windows in plug order and say nothing about
what is on the other end. It looks for, in order of preference, a CP210x
(`10C4:EA60` — the DevKitC-1's UART connector), an Espressif native-USB device
(`303A:*`), a CH340 (`1A86:*`), then an FTDI (`0403:*`).

```bash
py -3 detect_dots.py --list-ports     # or: py -3 serial_link.py --list
    COM1     ----:----  Communications Port (COM1)
  * COM10    10C4:EA60  Silicon Labs CP210x USB to UART Bridge   <- CP210x - DevKitC USB/UART
    COM7     ----:----  Standard Serial over Bluetooth link (COM7)
```

A `*` marks a candidate. If several match, the first is taken and the rest are
printed — pass `--port COMx` to settle it. **Another USB-serial adapter of the
same family will be picked up too**, so if you keep an FTDI or CH340 gadget
plugged in, name the port rather than trusting the guess.

## The window

Every frame is rendered with its detections drawn on it:

- **red circle + cross** — the red dot;
- **blue circles** — every black dot found;
- **green circle + tilted cross** — the one chosen as the target;
- **white arrow** — the error vector, tail on the laser, head on the target;
- top-left readout — what was found, the exact frame being sent, fps, counters.

Keys: `q` quit · `f` fire · `d` toggle the binary masks · `SPACE`/`n` next image
(folder mode).

### Seeing what the ESP32 says

The link is PC → ESP32 only, but the firmware console shares that UART, so its
`ESP_LOGI` output arrives on the same port. The script reads it every frame
(unread bytes would otherwise fill the OS buffer) and shows the newest line in
the window:

```
esp32 | I (5210) TRACK: ex:-0.094 ey:-0.195 vpan:-3.3 vtilt:6.8 st:TRACK
```

That is the firmware's own view of the error you just sent — the quickest way to
catch a sign or scaling mistake. Add `--echo` to print every line to the console
as well, or listen without sending anything at all:

```bash
py -3 serial_link.py --monitor      # print-only; the gimbal never moves
```

**FIRE button** (bottom-left, or the `f` key) sends `F\n` — one shot, the same
action as a short click on the board button. `ErrorVectorInput` accepts it only
as a line containing nothing but `F`, so firmware log text echoing on the shared
UART can never trigger a shot.

Its **border reports the state of the loop**, so you can watch one thing instead
of reading numbers:

| Border | Meaning |
|---|---|
| green | still converging — the error is too big to shoot on |
| red | `\|error\| ≤ --ready-error` (default 0.02): on target |
| blinking white/amber | the ESP32 just reported **arrival** |

Arrival is the firmware's own signal, not ours: when both axes settle inside its
deadzone it sends one `A <ex> <ey>` line back (`REPORT_ARRIVAL` in `Config.hpp`),
which the script parses and flashes the border four times for. Red says *the
camera* thinks you are on target; a blink says *the gimbal* agrees and has
stopped. The console prints the arrival too:

```
ARRIVAL  esp32 error +0.0012 -0.0031
```

`--ready-error` only changes the colour — the firmware decides arrival on its
own, much tighter deadzone (`TRACK_DEADZONE`, 0.004).

## Which black dot is the target

`--target center` (default) takes the black dot nearest the frame centre — aim
the camera to choose. `--target largest` takes the biggest, `--target nearest`
the one closest to the red dot.

## Tuning

Run with `--debug`: the second window shows the two binary masks that everything
else is derived from. Fix the masks and the detection follows.

Areas are given for a **640×480 reference frame** and scale automatically with
resolution, so they stay meaningful at 1280×720.

| Symptom | Knob |
|---|---|
| Red dot missed (pale / dim) | lower `--red-min-redness` |
| Noise detected as a dot when there is none | raise `--red-min-redness` |
| Red blob too big / bleeding into surroundings | raise `--red-rel` |
| Red distractor picked instead of the dot | lower `--red-area-max`, raise `--red-circ` |
| Black dots missed | raise `--black-darkness` toward 1.0, lower `--black-offset` |
| Shadows / paper edges detected as dots | lower `--black-darkness`, raise `--black-offset`, raise `--black-circ` |
| A coloured object detected as a dot | lower `--black-sat-margin` |
| Dot on strongly coloured paper missed | raise `--black-sat-margin` |
| Text / logos detected | raise `--black-circ`, raise `--black-area-min` |
| Big dots missed at close range | raise `--black-block` (≈3× dot diameter, odd) |

### Why the thresholds are relative

Both detectors deliberately avoid absolute colour gates, because a real scene is
rarely neutral. Measured off a live frame lit by a blue-ish lamp:

| | saturation | value |
|---|---|---|
| white paper | 105 | 167 |
| the black printed dot | 135 | 91 |
| the red laser dot | 52–122 | 139–255 |

An absolute "ink is unsaturated" rule (`S < 90`) throws the real dot away — the
paper itself is more saturated than that. So the dot is compared with the ring
of paper immediately around it instead: **darker than its own surroundings**
(`--black-darkness`) and **not much more colourful than them**
(`--black-sat-margin`).

The laser has the mirror-image problem: its core clips to white, so saturation
runs as low as 52 and no saturation gate can separate it from warm clutter.
It is found on **redness**, `R − max(G, B)`, where white scores 0 and only truly
red pixels score at all, thresholded relative to the frame's own peak.

## Protocol

`docs/uart-protocol.md` is the contract; `src/inputs/ErrorVectorInput.hpp` is
the receiving end. In short: `E <dx> <dy> <valid>\n` at 15–30 Hz, 115200 8N1,
`±1.0` spans half the frame, `valid = 0` when either dot is missing (keep
sending — silence for 300 ms trips the failsafe and resets the PIDs).

Bring-up, before connecting the camera:

```bash
py -3 serial_link.py --dx 0.2    # constant pan error: does it turn the right way?
py -3 serial_link.py --sweep     # slow circle on both axes
py -3 serial_link.py --fire      # one flash
```

(`serial_link.py` defaults to `--port auto`; add `--port COMx` to override,
`--echo` to see the firmware's log lines coming back.)

## Tuning over the wire

The firmware's PID console (`docs/pid-experiments.md`) is driven from here too,
so an experiment is one command line rather than a reflash:

```bash
py -3 serial_link.py --query                     # Q: what are the gains now?
py -3 serial_link.py --gains b 40 4 0            # K: set both axes
py -3 serial_link.py --gains tilt 35 6 0         # ... or one (p/pan, t/tilt, b/both)
py -3 serial_link.py --telemetry 1 --nudge 8 0   # T 1, then an 8-degree kick
py -3 serial_link.py --console                   # type frames by hand
```

Flags compose in protocol order (gains → telemetry → nudge → query) and every
reply within 400 ms is printed **decoded**, not raw:

```
-> K b 40 4 0
  gains: pan kp=40 ki=4 kd=0 | tilt kp=40 ki=4 kd=0 | ARMED
  ex:-0.124 ey:+0.058 pan:57.4 tilt:88.2 [TRACK]
  ARRIVED on target, residual +0.0012 -0.0031
```

`--console` is the interactive form: type `Q`, `K b 40 4 0`, `N 8 0`, `T 1`,
`F`, `E 0.2 0 1`; replies stream in on a background thread while you type.

Bad axis letters and negative gains are refused **here**, before they go out.
The firmware silently drops a malformed `K` and bumps a counter you cannot read
from the PC, which looks exactly like a command that worked and changed nothing.

In code:

```python
link.set_gains("b", 40, 4, 0)   # K b 40 4 0
link.nudge(8, 0)                # N 8 0     open-loop disturbance, degrees
link.telemetry(True)            # T 1
link.query()                    # Q
for line in link.poll():        # replies: parse_gains / parse_telemetry / parse_arrival
    ...
```

If an axis runs the wrong way, flip `PAN_INVERT` / `TILT_INVERT` in
`src/Config.hpp` — not the sign on the PC side.
