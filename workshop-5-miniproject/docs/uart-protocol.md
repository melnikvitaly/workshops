# UART contract — PC vision → ESP32 gimbal

This is the complete contract for whoever implements the PC side. The firmware
in `src/` implements the receiving end exactly as described here.

## Transport

| | |
|---|---|
| Port | The ESP32-S3 DevKitC-1 **UART** connector (CP2102), i.e. UART0 / GPIO43–44 |
| Baud | 115200 |
| Framing | 8 data bits, no parity, 1 stop bit, no flow control |
| Direction | Mostly PC → ESP32. The firmware sends exactly one kind of frame back: `A`, on arrival. |

**The console shares this port.** `ESP_LOGI` output from the firmware comes back
on the same UART. The PC sender must therefore:

- **tolerate arbitrary text arriving on RX** — log lines, boot messages, the
  ROM bootloader banner — and simply ignore anything it did not expect;
- never assume the port is silent or that its own bytes echo back.

If you want a clean channel later, moving the firmware console to the native USB
port frees this UART entirely; that is a one-line sdkconfig change and no change
to this protocol.

## Frame format

One frame per line, ASCII, terminated by `\n` (a preceding `\r` is accepted and
ignored). Two frame types:

```
E <dx> <dy> <valid>\n      tracking error (streamed continuously)
F\n                        fire one shot (one-shot, on demand)
```

Plus a small tuning console, documented in
[`pid-experiments.md`](pid-experiments.md) — not needed for normal operation:

```
K <axis> <kp> <ki> <kd>\n  set PID gains live; axis = p | t | b(oth)
N <dpan> <dtilt>\n         nudge the gimbal open-loop, in degrees
T <0|1>\n                  telemetry stream off / on
Q\n                        print current gains and state
```

`K` rejects negative gains (they invert the loop). `N` displaces the gimbal
without informing the controller, so the loop sees a pure disturbance — a
repeatable step input for comparing gain sets. `Q` and `K` both reply with a
`G pan <kp> <ki> <kd> tilt <kp> <ki> <kd> armed <0|1>` line.

### `E` — the error frame

| Field | Type | Meaning |
|---|---|---|
| `E` | literal | Frame tag. Upper or lower case. |
| `dx` | float | Horizontal error, **normalised** to `[-1, 1]` |
| `dy` | float | Vertical error, **normalised** to `[-1, 1]` |
| `valid` | int | `1` = both the laser dot and the target were seen this frame; `0` = not |

Fields are separated by whitespace (one or more spaces or tabs). Leading
whitespace before `E` is allowed. Floats may use any format `strtof` accepts —
`-0.124`, `.5`, `1e-2`. Do **not** send a decimal comma.

Example stream:

```
E -0.124 0.058 1
E -0.090 0.041 1
E -0.031 0.012 1
E 0.000 0.000 0        <- target or dot lost this frame
E -0.028 0.010 1
```

### Definition of the error vector

```
error = target_position - laser_dot_position
```

Both measured in the same camera frame, then normalised so that **±1.0 spans
half the frame** in that axis:

```
dx = (target_px_x - dot_px_x) / (frame_width_px  / 2)
dy = (target_px_y - dot_px_y) / (frame_height_px / 2)
```

So `dx = -0.5` means the dot is half a half-frame to the **right** of where it
should be. Use the image's native Y direction (Y growing downward is normal and
expected — the firmware compensates via `TILT_INVERT` in `Config.hpp`).

Values outside `[-1, 1]` are clamped by the firmware rather than rejected, so
there is no need to clip on the PC side.

### The `valid` flag

Send `valid = 0` whenever you could not measure a real error this frame —
target not found, laser dot not found, frame dropped, detector unsure. Keep
sending frames at your normal rate while this is the case; do not go silent.

On `valid = 0` the firmware **holds the gimbal still but keeps the PID
integral**, on the assumption that the occlusion is brief. Going silent instead
triggers the link timeout, which additionally resets the PID state.

`dx`/`dy` are ignored when `valid = 0`; sending `0 0` is conventional.

### `F` — the fire frame

```
F\n
```

A line containing nothing but `F` (upper or lower case, leading/trailing spaces
allowed) fires **one shot** — exactly what a short click on the board button
does.

Because the laser is normally held **constant on** (the camera needs the dot to
measure anything), a shot cannot be a pulse of light — it would be invisible
against an already-lit beam. So firing is the inverse: the beam is **blanked for
`LASER_FIRE_BLANK_MS` (120 ms) and then restored** to whatever the latch says.
The visible event is the gap.

**This is visible to your detector.** For those ~120 ms there is no dot in the
frame, so the honest thing to report is `valid = 0` — keep streaming at your
normal rate and let the gimbal hold. The blank is deliberately kept well under
the 300 ms link timeout so a shot never costs the PID its integral; a
`static_assert` in `Config.hpp` enforces that relationship.

Firing does **not** switch the laser on when it is off — there is simply no beam
to interrupt, and a 5 mW emitter should only ever light because someone asked it
to, never as a side effect of a "blink" command.

- Send it only when the operator asks for it. It is not part of the error
  stream and has no effect on the control loop.
- Anything else on the line — `F 1`, `FIRE`, `Fire at will` — is **rejected**,
  not fired. The firmware console shares this UART, and log text must never be
  able to trigger a shot.
- Fire is independent of the arm state: a disarmed gimbal does not move, but it
  still fires.
- One frame, one flash. Unlike `E` frames, `F` frames are never dropped or
  coalesced, which is why fire is its own frame rather than a fourth field of
  the error frame.

## Uplink: `A` — arrived on target

The one frame the firmware sends **back** to the PC:

```
A <ex> <ey>\n            e.g.  A -0.0021 +0.0034
```

Emitted the moment both axes settle inside the deadzone (`TRACK_DEADZONE`,
currently 0.004) — meaning the controllers have frozen and the gimbal has
stopped. `ex`/`ey` are the residual error **in the coordinates you sent**, not
the firmware's internal sign-corrected ones, so you can compare them directly
against the last `E` frame you transmitted.

**Edge triggered, one line per arrival.** It re-arms only after the dot leaves
the target again, or after the loop is disarmed, the link drops, or the target
is lost. It is not a periodic "still on target" heartbeat — if you need to know
the dot is *still* settled, track the absence of subsequent `E` corrections
rather than waiting for another `A`.

Parsing notes:

- It arrives interleaved with `ESP_LOGI` output on the shared UART. Match on a
  line beginning with `A ` followed by whitespace; log lines begin with `I (`.
- It is written unbuffered and flushed, so there is no latency between the
  gimbal stopping and the line appearing.
- Disable it entirely with `REPORT_ARRIVAL = false` in `Config.hpp`.

Typical use: hold fire until the gimbal has actually settled, then send `F`.

## Rate and timing

- **Send 15–30 frames/second.** The firmware runs its control step at 50 Hz and
  reuses the last commanded rate between frames, so a slower stream still moves
  smoothly — it just responds more sluggishly.
- **Send at a steady rate.** The firmware measures `dt` from frame arrival
  times, so jitter shows up directly in the derivative and integral terms.
- **Do not batch or buffer.** Send each frame the moment the vision pipeline
  produces it. Latency is the single biggest limit on how high the PID gains can
  go — see the tuning note in `Config.hpp`.
- If several frames arrive between control steps, the firmware acts on the
  newest and discards the rest. An older error is simply a worse measurement of
  where the dot is *now*, so dropping it is correct.

## Failsafe

If **no valid frame arrives for 300 ms** (`TRACK_TIMEOUT_MS`), the firmware:

1. commands zero velocity on both axes — the gimbal stops where it is;
2. resets both PID controllers, discarding the accumulated integral;
3. shows amber on the status LED and logs `st:NOLINK`.

Motion resumes automatically on the next valid frame. The laser is **not**
switched off by the timeout — it stays lit so the dot remains visible for
reacquisition.

This is why a stalled or crashed PC-side script is safe: the gimbal stops rather
than coasting on a stale error.

## Robustness rules the firmware applies

You do not need to guard against these on the PC side, but knowing them helps
when debugging:

- Malformed lines are counted and discarded, never partially acted on.
- Lines longer than 95 characters are discarded through to the next `\n`.
- Empty lines are ignored.
- `NaN` and `Inf` are rejected.
- Errors within `±0.01` (1% of the frame) are treated as zero, so the gimbal
  stops instead of hunting against servo deadband.

## Bring-up checklist

Do this before turning the gains up — a sign error makes the loop run to its
stop instead of converging.

1. Flash, open the serial monitor, confirm `ready - laser tracking, closed loop over UART`.
2. Confirm the laser is lit (it latches on at boot) and the status LED is amber
   (`NOLINK` — nothing is sending yet).
3. Send a few frames by hand from a terminal, e.g. `E 0.2 0.0 1`. The LED should
   go green and the pan axis should start moving.
4. **Check the direction.** With a positive `dx`, the dot must move *toward*
   where a positive `dx` says the target is. If it runs the other way and slams
   into the travel limit, flip `PAN_INVERT` in `Config.hpp`. Repeat for `dy` /
   `TILT_INVERT`.
5. Send `E 0 0 1` and confirm the gimbal stops.
6. Stop sending and confirm it stops within ~300 ms and the LED goes amber.

Only then connect the real camera and start tuning `PAN_KP` / `TILT_KP`.

## Reference sender

Minimal Python, no camera — enough to verify the link and check axis directions:

```python
import serial, time

ser = serial.Serial("COM5", 115200, timeout=0)   # your port here

def send(dx, dy, valid=1):
    ser.write(f"E {dx:.4f} {dy:.4f} {int(valid)}\n".encode())

try:
    while True:
        send(0.2, 0.0)          # constant error -> gimbal should pan one way
        time.sleep(0.05)        # 20 Hz
finally:
    ser.close()
```

Replace the constant with your vision pipeline's output once the directions
check out.

> **Opening the port can hold the board in reset.** pyserial asserts DTR and RTS
> on open, and on a dev board those drive the auto-reset circuit (RTS→EN,
> DTR→IO0). If all you ever read back is a stream of `0x00` with no log text,
> that is what happened. Deassert them *before* opening:
>
> ```python
> ser = serial.Serial()
> ser.port, ser.baudrate, ser.timeout = "COM5", 115200, 0
> ser.dtr = ser.rts = False      # before open(), not after
> ser.open()
> ```

## The implementation

`camera/` is the PC side of this protocol, written against this document.

```bash
# the loop: OAK camera -> red dot + black target dot -> E frames
py -3 camera/detect_dots.py --port            # bare --port = find the board
py -3 camera/detect_dots.py --port COM10      # ... or name it
py -3 camera/detect_dots.py                   # detect only, send nothing
py -3 camera/detect_dots.py --list-ports      # which COM port is the board?

# the link alone, no camera - the bring-up checklist above
py -3 camera/serial_link.py --dx 0.2          # step 3: constant pan error
py -3 camera/serial_link.py --sweep           # slow circle on both axes
py -3 camera/serial_link.py --fire            # one F frame
py -3 camera/serial_link.py --monitor         # listen only; sends nothing

# the tuning console of pid-experiments.md
py -3 camera/serial_link.py --query                    # Q
py -3 camera/serial_link.py --gains b 40 4 0           # K b 40 4 0
py -3 camera/serial_link.py --telemetry 1 --nudge 8 0  # T 1, then N 8 0
py -3 camera/serial_link.py --console                  # type frames by hand
```

One-shot flags compose in protocol order — gains, then telemetry, then nudge,
then query — so a whole experiment is one command line, and every reply the
firmware sends within 400 ms is printed decoded:

```
$ py -3 camera/serial_link.py --gains b 40 4 0 --query
-> K b 40 4 0
-> Q
  gains: pan kp=40 ki=4 kd=0 | tilt kp=40 ki=4 kd=0 | ARMED
```

Which part of this contract each piece implements:

| | |
|---|---|
| `E` frames | `detect_dots.py`, one per camera frame, capped by `--rate` (default 30 Hz), `valid = 0` whenever either dot is missing |
| `F` frames | the FIRE button in the window, the `f` key, or `serial_link.py --fire` |
| `A` uplink | parsed by `serial_link.parse_arrival`; the window blinks the FIRE border and the console prints `ARRIVAL esp32 error <ex> <ey>` |
| `K` `N` `T` `Q` | `ErrorLink.set_gains` / `.nudge` / `.telemetry` / `.query`, or the `--gains` / `--nudge` / `--telemetry` / `--query` flags |
| `G` uplink | `serial_link.parse_gains` → `{pan, tilt, armed}`, printed decoded |
| telemetry lines | `serial_link.parse_telemetry` → `{ex, ey, vpan, vtilt, pan, tilt, st}` |
| console text | read and discarded every frame (`--echo` prints it, the newest line always shows in the window) |
| DTR/RTS | deasserted **before** the port is opened, so opening it does not reset the board through the auto-reset circuit |

The axis letter and the no-negative-gains rule are enforced on the PC side too.
The firmware's answer to a bad `K` is to drop it and bump a counter you cannot
read from the PC — indistinguishable from a command that worked and did nothing
— so `set_gains` raises instead. `p`/`pan`, `t`/`tilt`, `b`/`both` are accepted;
anything else is refused rather than truncated to its first letter, which would
silently retune the wrong axis.

`--monitor` is the safe first move on a new board: it opens the port, prints
what the firmware says, and sends nothing, so the gimbal cannot move.

See `camera/README.md` for detector tuning.
