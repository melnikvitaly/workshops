# Problems faced

## 1. Auto aim "blows up"

- Dot runs away, then `LINK_LOST` follows.
- Likely cause: error can reach `±2`, but firmware rejected anything above `±1`.
  Rejected frames did not refresh link liveness, so the gimbal kept its last rate.

### Solution 1

- Error is clamped to `±1` in [`dots.py`](../../eye/camera/dots.py) and in
  [`Protocol.hpp`](../../firmware/aim/src/transport/Protocol.hpp).
- Small working zone (60° pan, 30° tilt) stays as a safety bound. See
  [`Config.hpp`](../../firmware/aim/src/Config.hpp).
- TODO: verify on the rig. PID tuning if it still oscillates.

## 2. Wrong MIN/MAX angles on the assembled gimbal

### Solution 2

- Limits measured on the rig.
- Direction flags set (more pan = left, more tilt = down).
- `static_assert` keeps the zone inside servo travel.
- TODO: add the measured limits.

## 3. Extra blinking of laser during startup

### Solution 3

- TODO: cause and fix.

## 4. Data stops reaching ESP32 when the PC window is resized

### Solution 4

- TODO: cause and fix.

## 5. Reset on disconnecting from UART0 on PC

- Cause: DTR/RTS toggle pulses the RST/EN pin (USB-UART bridge is wired to EN/GPIO0).

### Solution 5

- `serial_link.py` forces `dtr=False, rts=False`.
- TODO: confirm this fixes it. A serial monitor can still trigger it.

## 6. Non-linear dependency between positions on the wall and angles of servos

### Solution 6

- TODO: cause and fix.

## 7. Slow targeting

### Solution 7

- TODO: cause and fix.

## 8. Dot detection is not reliable

- Detector can lose the dot for a few frames, then find it again ("blink").
- Effect: error jumps or drops out, so aim can twitch or stop for a moment.
- Code: [`detect_dots.py`](../../eye/camera/detect_dots.py).

### Solution 8

- TODO: cause and fix (for example: hold last position for a short time,
  filter jumps, tune thresholds).

## 9. Laser moves outside the camera view zone

- The gimbal can point the laser outside what the camera sees.
- The dot is not detected, so no error is sent and aim cannot correct itself.
- Related: [problem 8](#8-dot-detection-is-not-reliable) (dot lost for a few frames).

### Solution 9

- New [`recenter.py`](../../eye/camera/recenter.py), used from
  [`detect_dots.py`](../../eye/camera/detect_dots.py).
- Red dot missing for `--recenter-ms` (default 1500): the gimbal moves in small
  `P` steps toward the centre of the working zone (`--recenter-speed`, deg/s).
- It stops as soon as the red dot is seen again, then normal tracking resumes.
  If the centre is reached first, it holds there.
- On by default. Off with the **Recenter if laser lost** checkbox in the left
  panel or `--recenter-ms 0`.
- Centre is used because it is inside the camera view, so the dot can be found again.
- Zone limits: [`Config.hpp`](../../firmware/aim/src/Config.hpp).
- TODO: verify on the rig.

## 10. Camera autofocus

- The camera has autofocus turned on.
- Focus can change while the rig moves or the laser dot appears.
- Effect: image gets blurry, dot size and brightness change, so detection
  is less stable.
- Related: [problem 8](#8-dot-detection-is-not-reliable).
- Code: [`detect_dots.py`](../../eye/camera/detect_dots.py).

### Solution 10

- Autofocus is always off. The OAK lens is set to a fixed position
  (`--focus`, 0–255, default 130) when the pipeline starts, in
  [`detect_dots.py`](../../eye/camera/detect_dots.py).
- The **Lens focus** field in the Speed box ([`speed.py`](../../eye/camera/speed.py))
  changes it live, with no camera restart.
- Bench test (1280×720): sharpness was 28 at lens 130, but 15–17 at 0, 80, 180
  and 255, and 18 with autofocus. It stayed stable with the lens held.
- TODO: tune the value on the rig for the real wall distance.
