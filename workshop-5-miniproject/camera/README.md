# camera/ — the PC end of the loop

Finds the **red dot** (where the laser points now) and the **black printed dot**
(where it should point) in the OAK camera image, and streams the error between
them to the ESP32 over the COM port.

```text
OAK-1 ──USB──> PC: detect_dots.py ──COM──> ESP32-S3 ──> gimbal + laser
                    red dot, black dot        E <dx> <dy> <valid>
                    error = target − laser    F  (fire)
```

Detection runs on the **host** in plain OpenCV — the OAK is used as a camera
only. The camera plumbing (DepthAI v3) mirrors
`final_project/camera-host/detect.py`, so the same pipeline shape carries over;
the red-dot NN of that project is unnecessary here because a PC can run the
color filter directly.

| File                  |                                                                                    |
|-----------------------|------------------------------------------------------------------------------------|
| `detect_dots.py`      | main script: frame sources → detection → error vector → COM, plus the command line |
| `dots.py`             | the detection itself: red dot, black dots, target choice, error vector             |
| `serial_link.py`      | the COM link and the wire format; also a standalone sender for bring-up            |
| `overlay.py`          | what is drawn on each frame: detections, error arrow, status text, mask windows    |
| `fire_button.py`      | the on-screen FIRE button and its border states (converging / on target / arrived) |
| `controls.py`         | the controls window (Tk): gain presets, manual gains, nudge, telemetry, query      |
| `tuning.py`           | the `--debug` threshold sliders, and printing them back out as a command line      |
| `simulated_target.py` | click or arrow-key a stand-in target dot when no black dot is printed              |

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

Keys: `q` quit · `f` fire · `d` toggle the binary masks and the threshold
sliders · `p` print the current thresholds as a command line · arrows move the
simulated target · `SPACE`/`n` next image (folder mode).

Mouse (view window): left-click places or moves a **simulated target** where no
black dot is printed, right-click clears it. The arrow keys nudge it 24 px at a
time, in the direction it moves on screen even under `--rotate`; with no dot yet
the first arrow puts one at the frame centre.

### Seeing what the ESP32 says

The link is PC → ESP32 only, but the firmware console shares that UART, so its
`ESP_LOGI` output arrives on the same port. The script reads it every frame
(unread bytes would otherwise fill the OS buffer) and shows the newest line in
the window:

```text
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

| Border               | Meaning                                               |
|----------------------|-------------------------------------------------------|
| green                | still converging — the error is too big to shoot on   |
| red                  | `\|error\| ≤ --ready-error` (default 0.02): on target |
| blinking white/amber | the ESP32 just reported **arrival**                   |

Arrival is the firmware's own signal, not ours: when both axes settle inside its
deadzone it sends one `A <ex> <ey>` line back (`REPORT_ARRIVAL` in `Config.hpp`),
which the script parses and flashes the border four times for. Red says *the
camera* thinks you are on target; a blink says *the gimbal* agrees and has
stopped. The console prints the arrival too:

```text
ARRIVAL  esp32 error +0.0012 -0.0031
```

`--ready-error` only changes the colour — the firmware decides arrival on its
own, much tighter deadzone (`TRACK_DEADZONE`, 0.004).

### The controls window

A second window, `gimbal controls`, carries everything on the command side of
the protocol: **Query gains**, a **Telemetry** toggle, an axis + KP/KI/KD row
with **Set**, an open-loop **Nudge**, and the grid of gain presets. Clicking a
preset also loads its numbers into the KP/KI/KD fields, so it can be adjusted by
hand from wherever it landed. Results — and any refusal from the link — appear
on the status line at the bottom; `Q`'s reply comes back on the `esp32 |` line
over on the view.

It is a Tk window rather than a painted OpenCV one, which is why it has real
text fields. Tk ships with Python, so this costs no extra dependency; if it is
somehow unavailable the script says so and keeps tracking without the panel, and
closing the window does the same. It shares the main thread with the loop, so a
click has reached the serial link before the next frame is detected.

## Which black dot is the target

`--target center` (default) takes the black dot nearest the frame centre — aim
the camera to choose. `--target largest` takes the biggest, `--target nearest`
the one closest to the red dot.

## Tuning

Run with `--debug`: the mask window shows the two binary masks that everything
else is derived from, and every rejected blob is boxed in grey **on the frame
itself, labelled with the measurement that failed** — `circ 0.66`, `hollow 0.46`,
`pale 0.91`. That label names the knob, so tuning is reading rather than
guessing. The chosen target's `round` score is in the top-left readout; a target
hovering near a threshold is what a flickering lock looks like from here.

`--debug` also opens **`tune: red dot`** and **`tune: black dots`**, one slider
per threshold, so a value can be swept against a live frame instead of costing a
restart per guess. The flags below still set the starting point; the sliders take
over from there. Sliders are integers, so fractions carry their scale in the
name: `circ /100` at 80 is `0.80`, `area max x100` at 200 is `20000`.

The sliders die with the window, so `p` prints the current set as a command
line — that is how a tuning session becomes the next run's flags:

```text
--red-rel 0.5 --red-min-redness 22 ... --black-circ 0.66 --black-edge-margin -1
```

Areas are given for a **640×480 reference frame** and scale automatically with
resolution, so they stay meaningful at 1280×720.

| Symptom                                                      | Knob                                                        |
|--------------------------------------------------------------|-------------------------------------------------------------|
| Red dot missed (pale / dim)                                  | lower `--red-min-redness`                                   |
| Noise detected as a dot when there is none                   | raise `--red-min-redness`                                   |
| Red blob too big / bleeding into surroundings                | raise `--red-rel`                                           |
| Red distractor picked instead of the dot                     | lower `--red-area-max`, raise `--red-circ`                  |
| Black dots missed                                            | raise `--black-darkness` toward 1.0, lower `--black-offset` |
| Shadows / paper edges detected as dots                       | lower `--black-darkness`, raise `--black-offset`            |
| A coloured object detected as a dot                          | lower `--black-sat-margin`                                  |
| Dot on strongly coloured paper missed                        | raise `--black-sat-margin`                                  |
| Big dots missed at close range                               | raise `--black-block` (≈3× dot diameter, odd)               |
| Real dot rejected as `compact` (rough print)                 | lower `--black-compact`                                     |
| Real dot rejected as `circ` / `aspect` (steep viewing angle) | lower `--black-circ`, raise `--black-aspect`                |
| Something square-ish still accepted                          | lower `--black-radial` toward 0.05                          |

### Is it round?

Being dark is easy — shadows, text, a cable, the edge of the sheet and the gap
under a bulldog clip all manage it — so the shape test is what actually picks
the dot out of a scene. No single number does it, so six run, each blind to a
different impostor, and **the first one that fails is the label `--debug`
draws**:

| Measurement | What it is                                       | Catches                                                       | A disc measures | Default  |
|-------------|--------------------------------------------------|---------------------------------------------------------------|-----------------|----------|
| `circ`      | fraction of the smallest enclosing circle filled | squares, triangles, anything lopsided                         | 0.82–0.98       | ≥ `0.80` |
| `radial`    | spread of the centre-to-edge distance            | rounded squares — the shape everything else forgives          | 0.00–0.10       | ≤ `0.10` |
| `aspect`    | long/short side of the min-area rectangle        | ellipses, rounded bars                                        | 1.00–1.12       | ≤ `1.25` |
| `solid`     | area ÷ its own convex hull                       | dents and notches: two dots touching, a C                     | 0.88–0.99       | ≥ `0.88` |
| `compact`   | 4π·area ÷ perimeter²                             | frayed, knobbly outlines: shadow edges, joined-up text        | 0.45–0.91       | ≥ `0.50` |
| `hollow`    | enclosed background ÷ blob area                  | rings, an O, a washer — *perfect* circles to every test above | 0.00            | ≤ `0.15` |

Plus an `edge` gate: a blob touching the frame border is a partial outline, and
its true centre is outside the picture anyway (`--black-edge-margin -1` keeps
them).

The thresholds are measured rather than guessed — rendered discs against
near-misses at radii 7–45 px — and the margin is real: an axis-aligned square
scores `circ` 0.72 against the disc's 0.82 floor, a 0.78-ratio ellipse 0.77, a
printed letter 0.73. On a test frame carrying a dot plus a square, a rotated
square, an ellipse, a ring, a cable, a shadow and a line of text, one blob is
accepted.

The one near-miss deliberately let through is a regular **hexagon or octagon**:
separating those from a disc costs more real dots than it saves, and at these
sizes they are circles as far as aiming is concerned. Drop `--black-radial` to
`0.05` if you disagree.

Only the black dot is judged this hard. The red gate stays loose on purpose — at
range the laser is a handful of pixels, where every one of these measurements is
noise, and there is only ever one red thing in the frame.

### Why the thresholds are relative

Both detectors deliberately avoid absolute colour gates, because a real scene is
rarely neutral. Measured off a live frame lit by a blue-ish lamp:

|                       | saturation | value   |
|-----------------------|------------|---------|
| white paper           | 105        | 167     |
| the black printed dot | 135        | 91      |
| the red laser dot     | 52–122     | 139–255 |

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

```text
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
