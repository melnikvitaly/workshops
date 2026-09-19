# Problems faced

## 1. Auto aim "blows up"

- Dot runs away, then `LINK_LOST` follows.
- Likely cause: error can reach `±2`, but firmware rejected anything above `±1`.
  Rejected frames did not refresh link liveness, so the gimbal kept its last rate.

### Solution 1

- Error is clamped to `±1` in [`dots.py`](eye/camera/dots.py) and in
  [`Protocol.hpp`](firmware/aim/src/transport/Protocol.hpp).
- Small working zone (60° pan, 30° tilt) stays as a safety bound. See
  [`Config.hpp`](firmware/aim/src/Config.hpp).
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
- Code: [`detect_dots.py`](eye/camera/detect_dots.py).

### Solution 8

- TODO: cause and fix (for example: hold last position for a short time,
  filter jumps, tune thresholds).
