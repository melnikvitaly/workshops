# camera/ — the PC end of the loop

Finds the **red dot** (where the laser points now) and the **black printed dot**
(where it should point) in the OAK camera image, and streams the error between
them to the ESP32 over the COM port.

```text
OAK-1 ──USB──> PC: tracker.py ──COM──> ESP32-S3 ──> gimbal + laser
                    red dot, black dot        E <dx> <dy> <valid>
                    error = target − laser    F  (fire)
```

Detection runs on the **host** in plain OpenCV — the OAK is used as a camera
only. The camera plumbing (DepthAI v3) mirrors the on-camera NN pipeline used
elsewhere, so the same pipeline shape carries over; the red-dot NN is
unnecessary here because a PC can run the color filter directly.

| File                  |                                                                                    |
|-----------------------|------------------------------------------------------------------------------------|
| `tracker.py`      | main script: frame sources → detection → error vector → COM, plus the command line |
| `dots.py`             | the detection itself: red dot, black dots, target choice, error vector             |
| `serial_link.py`      | the COM link and the wire format; also a standalone sender for bring-up            |
| `app_window.py`       | the one Tk window: controls left, camera view + FIRE middle, tuning right          |
| `overlay.py`          | what is drawn on each frame: detections, error arrow, mask image; status text      |
| `controls.py`         | the left panel and action buttons: gain presets, manual gains, nudge, zone, query  |
| `manual_control.py`   | keyboard-driven `MANUAL` channel: arrow keys → `M <vpan> <vtilt>` frames           |
| `tx_log.py`           | the one place every line sent to the ESP32 is logged (console and/or file)         |
| `tuning.py`           | the threshold sliders (right panel), and printing them back out as a command line  |
| `speed.py`            | the Speed box (right panel): camera fps, frame queue, lens focus, send rate        |
| `simulated_target.py` | click or arrow-key a stand-in target dot when no black dot is printed              |

## Install and run

```bash
py -3 -m pip install -r requirements.txt

py -3 tracker.py --port                # live OAK -> ESP32, port auto-detected
py -3 tracker.py --port COM5           # ... or name it
py -3 tracker.py                       # live OAK, detection only, nothing sent
py -3 tracker.py --source shot.jpg --debug     # tune thresholds on one image
py -3 tracker.py --source frames/ --debug      # step through a folder
py -3 tracker.py --source 0                    # any USB webcam, no OAK needed
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
py -3 tracker.py --list-ports     # or: py -3 serial_link.py --list
    COM1     ----:----  Communications Port (COM1)
  * COM10    10C4:EA60  Silicon Labs CP210x USB to UART Bridge   <- CP210x - DevKitC USB/UART
    COM7     ----:----  Standard Serial over Bluetooth link (COM7)
```

A `*` marks a candidate. If several match, the first is taken and the rest are
printed — pass `--port COMx` to settle it. **Another USB-serial adapter of the
same family will be picked up too**, so if you keep an FTDI or CH340 gadget
plugged in, name the port rather than trusting the guess.

## The window

One window, three columns. Each side panel folds away with the arrow button on
its edge; the tuning panel on the right starts folded.

| Column | Holds                                                                                           |
|--------|-------------------------------------------------------------------------------------------------|
| Left   | the settings: telemetry, channel, PID gain table + presets, working zone, nudge, status         |
| Middle | the camera view, and under it **FIRE**, keyboard drive and the action buttons (see below)       |
| Right  | **Debug view**, the mask image, the red / black threshold sliders, the tracking-error graph, **Pin left edge** / **Snapshot** |

Under the view: **FIRE** (red border colour = on target, green = converging),
**Keyboard drive**, **Arm / Disarm (CONTROL)**, **Center**, **Start Zone Tour**
and **Query gains**. The row wraps when the view is narrow.

Every frame is rendered with its detections drawn on it:

- **red circle + cross** — the red dot;
- **blue circles** — every black dot found;
- **green circle + tilted cross** — the one chosen as the target;
- **white arrow** — the error vector, tail on the laser, head on the target;
- top-left readout — what was found, the exact frame being sent, fps and
  frame size (`WxH`), counters.

Keys (they work while the view has focus; click it to give it back after using
a text box): `q` quit · `f` fire · `d` toggle the mask image and the labelled
rejections · `p` print the current thresholds as a command line · `m` toggle
keyboard `MANUAL` drive · arrows move the simulated target, or drive the
gimbal while `m` is engaged · `SPACE`/`n` next image (folder mode).

Mouse (on the view): left-click places or moves a **simulated target** where no
black dot is printed, right-click clears it. The arrow keys nudge it 24 px at a
time, in the direction it moves on screen even under `--rotate`; with no dot yet
the first arrow puts one at the frame centre. While keyboard `MANUAL` drive is
engaged, the arrow keys drive the gimbal instead (see below) and no longer
touch the simulated target.

### The tracking-error graph

The right panel plots pan/tilt error against sample number. By default it
slides, always showing the last 300 samples.

- **Pin left edge** — freezes the left edge at whatever sample is on screen
  when you tick it. New samples keep growing the plot to the right instead of
  scrolling old ones off, so a whole run stays visible from that point.
  Untick to go back to the sliding 300-sample window. The plot still keeps
  20000 samples in memory; past that the pinned edge follows the oldest
  sample still held.
- **Snapshot** — saves the plot as a PNG into `eye/camera/snapshots/`
  (git-ignored), named with the date/time and the pan/tilt gains last sent
  with **Apply** in the left panel (`gains-unset` if none have been applied
  yet), e.g. `error_20260922-143012_pan-P40-I4-D6_tilt-P35-I4-D5.png`. The
  same gains and timestamp are also written into the saved image's title, so
  the file is self-describing once it leaves this folder.

### Driving the gimbal from the keyboard

Press `m` to toggle **keyboard `MANUAL` drive** (`manual_control.py`). Turning
it on also sets `input.channel = MANUAL` on the board — the same effect as
picking `MANUAL` + **Set** in the left panel — then the arrow keys send
`M <vpan> <vtilt>` frames (`docs/protocol.md`) for as long as they are held,
at `--manual-speed` deg/s per axis (default 40). Turning it off sends one
immediate zero-velocity frame and stops sending; the gimbal still needs
`ARMED` (`--arm`, or the **Arm / Disarm** button) to actually
move, same as `AUTO`.

MANUAL fails safe the same way AUTO does — 300 ms without a fresh `M` frame
parks the gimbal — so `manual_control.py` keeps a keep-alive frame going
(every 150 ms) the whole time a key is down, even if the commanded velocity
has not changed, and sends immediately the moment it does change. "Held" is
read straight from the OS key state (Windows only), not guessed from
key-repeat events, so release is immediate and exact — see the module
docstring.

Driving one frame at a time, without the keyboard, from the command line or
the interactive console:

```bash
py -3 serial_link.py --channel MANUAL --arm --manual 20 0       # one M frame
py -3 serial_link.py --console                                  # then type: M 20 0
```

### Telemetry from the ESP32

Console logging is on a separate UART (see `serial_link.py`'s module
docstring), so this port carries only control ASCII and NDJSON lines.
Telemetry is **on by default** — the firmware itself boots with it off, but
`tracker.py` keeps asking with `T 1` until a sample arrives, which also
recovers it after a board reset without touching anything — so there is no
separate step to see it. Untick the checkbox in the controls panel, or send
`T 0`, to turn it back off; the UI renders the latest `tlm` sample
(`docs/protocol.md` §3.4), tagged with how long ago it arrived:

```text
ESP T  st:ARMED ch:AUTO  ex:-0.031 ey:+0.012  v:-4.2/+1.1 deg/s  pan:92.4 tilt:78.1  0.3s ago
PID: 29.8 Hz   0.6s ago
```

The `PID:` line comes from the 1 Hz `tlm.sys` message (`pid_hz`): how many times
per second the ESP itself ran the PID, measured over the last second. It is `0.0`
while the loop is idle (no target, not armed, or link lost).

That is the firmware's own view of the error you just sent, plus its state,
channel and angles — the quickest way to catch a sign or scaling mistake. The
sample is never hidden once received: the age keeps counting up if telemetry
stops arriving, so a dead link reads as a growing "Xs ago" instead of the
readout vanishing. Add `--echo` to print every received line to the console as
well, or listen without sending anything at all:

```bash
py -3 serial_link.py --monitor      # print-only; the gimbal never moves
```

### Logging sent commands

Every line written to the ESP32 — `E`/`M` frames, `F`, `K`, `N`, `T`, `Q`, and
NDJSON `cfg.set` — passes through one place, `tx_log.py`, instead of each
sender printing (or not printing) it on its own. Two independent outputs:

- **Console** — everything prints as `-> ...` (`F`, `K`, `N`, `T`, `Q`,
  `cfg.set`, and keyboard `MANUAL` drive's `M` frames), except `E`. `E` is
  the one tag sent continuously regardless of any key or click — up to
  `--rate`, 50 Hz by default, for as long as `AUTO` runs — so printing every
  one by default would drown everything else; add `--echo` to also print
  those, and received lines too. `M` gets no such throttling: it only goes
  out while a direction key is actually held, so it stays visible the same
  way `F` or `N` is.
- **File** — `--tx-log [PATH]` (bare `--tx-log` = `tx_log.txt`) appends
  *every* line, streamed frames included, one per row with the elapsed time
  since the log opened. Nothing is filtered out of the file regardless of
  `--echo`, so it is the one to reach for when reviewing what was actually
  sent during a run:

```bash
py -3 tracker.py --port --tx-log session.log
py -3 serial_link.py --console --tx-log            # tx_log.txt in the cwd
```

**FIRE button** (bottom-left, or the `f` key) sends `F\n` — one shot, the same
action as a short click on the board button. `ErrorVectorInput` accepts it only
as a line containing nothing but `F`, so firmware log text echoing on the shared
UART can never trigger a shot.

Its **border reports the state of the loop**, so you can watch one thing instead
of reading numbers:

| Border | Meaning                                               |
|--------|--------------------------------------------------------|
| green  | still converging — the error is too big to shoot on   |
| red    | `\|error\| ≤ --ready-error` (default 0.02): on target |

`--ready-error` only changes the colour — the firmware decides arrival on its
own, much tighter deadzone (`TRACK_DEADZONE`, 0.004), which is not carried on
the `tlm` sample; the `st`/`ch` fields in the telemetry readout are the
firmware's own state and channel instead.

### Making the PC the active channel

Sending `E` frames is not enough by itself. The firmware picks exactly one
input channel at a time (`input.channel`: `NONE` / `AUTO` / `MANUAL` /
`AUTO_POS`) and boots at `NONE`, so `E` frames are parsed, counted as
`drop_inact`, and thrown away until something sets it to `AUTO` — that is
this script's channel, not `MANUAL` (the keyboard-driven `M <vpan> <vtilt>`
channel, see
[Driving the gimbal from the keyboard](#driving-the-gimbal-from-the-keyboard))
or `AUTO_POS` (the same `E` frames, driven through a direct-position PID
instead of `AUTO`'s velocity-form one — see
[`docs/servo-control-strategies.md`](../../docs/servo-control-strategies.md)).

Unlike telemetry, this is never sent automatically — forcing the channel over
can take control away from whatever else is driving the gimbal (another PC
session, the pilot remote), so it always waits for an explicit action:

```bash
py -3 serial_link.py --channel AUTO     # cfg.set input.channel AUTO, then exit
```

or the **Channel** dropdown + **Set** button in the left panel (below).
Either way it is a `cfg.set` NDJSON message, acknowledged by a `cfg.state`
reply (`docs/protocol.md` §3.3) printed as `cfg: input.channel='AUTO' OK
[id N]` with `--echo`/`--console`/`--monitor`; confirm it actually took by
watching `ch:` in the telemetry readout on the view.

### Arming

Selecting `AUTO` is still not enough — the gimbal only moves while the
firmware's own state machine is `ARMED` (check `st:` in the telemetry
readout). Arming used to be physical-button-only; `arm`/`disarm` are their
own wire commands (not `cfg.set`, no ack — same shape as `estop`) that do
exactly what the board's `CONTROL` button does, so `EYE` can arm or disarm
remotely too:

```bash
py -3 serial_link.py --arm       # {"t":"arm"}    - DISARMED/PARKED -> ARMED
py -3 serial_link.py --disarm    # {"t":"disarm"} - ARMED/LINK_LOST -> DISARMED
```

Each no-ops outside its matching state, and neither clears a latched `FAULT`
— that stays `fault.ack`. Watch `st:` in the telemetry readout to confirm
what happened; see `docs/protocol.md` §5.

The **Arm / Disarm (CONTROL)** button under the view is still one button: it
tracks the last-seen `st:` and picks `arm`, `disarm` or `fault.ack` itself,
the same decision `Ui::controlPressed()` makes on the board. This trades away
the safety property physical-button-only arming gave (nobody can arm the
gimbal without standing at the board) for bench-testing convenience — see
`docs/protocol.md` §5 before wiring it into anything unattended.

### The controls

The left panel of the window, plus the buttons under the view, carry everything
on the command side of the protocol: a **Telemetry** toggle, a **Channel**
selector, an axis + KP/KI/KD row with **Set**, the gain presets, a **Working
zone** box, and an open-loop **Nudge**, in that order. The one-shot buttons
under the view are **Arm / Disarm (CONTROL)**, **Center**, **Start Zone Tour**
and **Query gains**. Nudge moves the physical gimbal by the entered number of
degrees, simulating a sudden bump, vibration, wind gust, or mechanical slip; it
is a repeatable disturbance for checking how the loop recovers, not an aiming
offset. Clicking a
preset also loads its numbers into the KP/KI/KD fields, so it can be adjusted by
hand from wherever it landed. Results — and any refusal from the link — appear
on the status line at the bottom; `Q`'s reply comes back on the `esp32 |` line
over on the view.

### Laser lost: move toward the centre

The gimbal can point the laser outside the camera view. The red dot is then
not found, so there is no error to correct and the firmware only holds.

| Item       | Behaviour                                                            |
|------------|----------------------------------------------------------------------|
| Trigger    | red dot missing for `--recenter-ms` (default 1500). Black dot missing does not trigger it |
| Motion     | small `P` steps toward the zone centre at `--recenter-speed` deg/s (default 30) |
| Stop       | as soon as the red dot is seen again, or when the centre is reached  |
| Default    | on. Untick **Recenter if laser lost** in the left panel, or use `--recenter-ms 0` |
| Not active | while keyboard MANUAL drive is on, or with no serial port            |

The start position comes from the `tlm` sample. With no fresh sample the
gimbal jumps to the centre in one `P` move instead, which cannot be stopped
half way. Code: [`recenter.py`](recenter.py).

### Working zone and zone tour

The **Working zone** box sets the four `zone.pan.min/max`,
`zone.tilt.min/max` bounds — the area the gimbal is allowed to move in,
narrower than (and clamped to) the mechanical limits. **Set zone** applies
the fields live, no reboot needed; the fields themselves start at the
firmware's compiled defaults and are only a starting point to edit, not a
mirror of the board's current value, unless you have just clicked Set Max
Zone or Set zone yourself. **Set Max Zone** reads the gimbal's hard
mechanical travel live from the board (`zone.limit.{pan,tilt}.{min,max}`, a
read-only `cfg.get` — see `docs/protocol.md` §3.3) and applies it in one
click; it briefly waits for the board's reply, so it needs a connected,
responding board. **Start Zone Tour** re-enters the `ZONE_TOUR` state — the
same walk around the zone's perimeter, laser lit, that runs at boot when
**Tour on boot** is ticked (`--boot-tour on|off`; saved in the board's NVS,
default off, applies at the next reset) — so a new zone can be watched
without power-cycling the board. It only
starts while the gimbal is `DISARMED`/`PARKED` (check `st:` in the
telemetry readout); requesting it from any other state is silently
ignored. **Center** reads the *current* working zone live (not the entry
fields) and drives straight to its midpoint with a `P <pan> <tilt>`
absolute-position frame — `Gimbal::moveTo()`, bypassing the PID and the
selected channel entirely, the same primitive `N` (nudge) uses but for an
absolute angle instead of an offset. The equivalent from `serial_link.py` is
`--zone PAN_MIN PAN_MAX TILT_MIN TILT_MAX`, `--zone-tour`,
`--boot-tour on|off`, `--move-to PAN
TILT`, and `--center`.

The whole window is Tk rather than a painted OpenCV one, which is why it has
real text fields and buttons. Tk ships with Python, so this costs no extra
dependency, and the camera view is shown as a PPM image, so there is no Pillow
either. There is no OpenCV window and no `cv2.waitKey`. Closing the window ends
the run. It shares the main thread with the loop, so a click has reached the
serial link's queue before the next frame is detected.

## Which black dot is the target

`--target center` (default) takes the black dot nearest the frame centre — aim
the camera to choose. `--target largest` takes the biggest, `--target nearest`
the one closest to the red dot.

## Tuning

Turn on **Debug view** (or `--debug`, or `d`): the right panel shows the two
binary masks that everything else is derived from, and every rejected blob is
boxed in grey **on the frame itself, labelled with the measurement that failed**
— `circ 0.66`, `hollow 0.46`, `pale 0.91`. That label names the knob, so tuning
is reading rather than guessing. The chosen target's `round` score is in the
top-left readout; a target hovering near a threshold is what a flickering lock
looks like from here.

The right panel has one tab per detector, **Red dot** and **Black dots**, with a
slider per threshold, so a value can be swept against a live frame instead of
costing a restart per guess. The flags below still set the starting point; the
sliders take over from there. Each slider shows its real value, for example
`circ` at `0.80`.

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
the receiving end. In short: `E <dx> <dy> <valid>\n` at up to 50 Hz, 115200 8N1,
`±1.0` spans half the frame, `valid = 0` when either dot is missing (keep
sending — silence for 300 ms trips the failsafe and resets the PIDs).

### Speed settings

These set how fast the vision side feeds the firmware. The firmware uses at
most one frame per 20 ms control step, so more than 50 Hz is wasted.

The flags give the starting values. While running, the **Speed** box in the
right panel changes them: edit the numbers and press **Apply** (or Enter).
Bad numbers are refused and shown in the box.

| Setting      | Live change                                                       |
|--------------|-------------------------------------------------------------------|
| Send rate    | at once                                                           |
| Frame queue  | at once if the camera allows it, otherwise the camera restarts    |
| Camera fps   | the camera restarts (about a second, the view freezes)           |
| Lens focus   | at once (autofocus is always off, the lens stays at this value)  |

The OAK-1 sensor (IMX378, up to 4056×3040) only offers some size and fps
pairs. Measured on this camera:

| Size      | fps that work |
|-----------|---------------|
| 4056×3040 | 15, 30        |
| 3840×2160 | 15, 30        |
| 1920×1080 | 30, 45, 60    |
| 1280×720  | 15–60         |
| 640×360   | 15–60         |

If the camera refuses a pair, the run does not stop. It keeps the last fps
that worked (at startup it steps down to 30, then 15), and the Speed box
shows which fps is in use.

| Flag           | Default | Meaning                                                                 |
|----------------|---------|-------------------------------------------------------------------------|
| `--fps`        | 60      | OAK sensor frame rate                                                   |
| `--queue-size` | 1       | OAK frames buffered; 1 = always the newest frame (lowest latency)       |
| `--rate`       | 50      | max `E` frames per second put on the wire                               |
| `--focus`      | 130     | OAK lens position 0–255, autofocus off. Tune for the wall distance      |

Detection time per frame can still cap the real rate; the `fps` readout on
the view shows what the loop actually reaches.

All serial I/O runs on one worker thread inside `ErrorLink`
(`serial_link.py`). The main loop only queues lines (`send`, `set_gains`, …)
and collects replies with `poll()`; it never touches the port.

Windows blocks the main loop while a window is dragged or resized. The worker
keeps the link up: once the loop is quiet for 120 ms it sends a hold frame
(`E 0 0 0`, or `M 0 0` if MANUAL was last). The gimbal holds still and does
not chase the old target. Keepalive frames are not written to the tx log.

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
for line in link.poll():        # replies: parse_gains / parse_telemetry
    ...
```

If an axis runs the wrong way, flip `PAN_INVERT` / `TILT_INVERT` in
`src/Config.hpp` — not the sign on the PC side.
