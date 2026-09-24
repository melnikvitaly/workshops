# Bringup — `AIM`

First-power-on steps for the `AIM` board: flash order, what the first boot
looks like, and what the OLED and status LED say at each stage. Pin-level
detail is in [`interfaces.md`](./interfaces.md); wire formats are in
[`protocol.md`](./protocol.md); task and FSM detail is in
[`architecture.md`](./architecture.md).

---

## 1. What you need

- `AIM` board (ESP32-S3-WROOM-1) with servos, laser driver, SD card, OLED
  and buttons wired per [`interfaces.md` §1](./interfaces.md#1-pin-map).
- USB-C cable — native USB (GPIO19/20), used for flashing and the console
  (`UART0`). No data ever crosses this port.
- A second, separate USB-TTL adapter wired to `UART1` (GPIO17 TX / GPIO18
  RX) — this is the `EYE` data link, and is a different physical connection
  from the flashing cable.
- PC with [PlatformIO](https://platformio.org/) and Python 3 for `eye/camera`.

## 2. Flash order

1. Connect the board over USB-C (native USB / `UART0`).
2. Build and flash from `firmware/aim/`:

   ```bash
   pio run -e esp32-s3-devkitc-1
   pio run -t upload
   pio device monitor
   ```

3. Leave the monitor open — every boot and state transition is logged here
   (`ESP_LOGI`, tag `AIM`/`CTRL`/`UI`/…).
4. Connect the `UART1` USB-TTL adapter (a second cable/port) before starting
   `eye/camera` — the console on `UART0` never carries control data, so the
   two connections do not interfere.

Re-flashing does not need the `UART1` cable connected; console-only bringup
(steps 1–3) is enough to check the board is alive.

## 3. First boot — expected sequence

Console output on a clean first boot:

```text
AIM starting
boot: reason=POWERON schema=v1 config=defaults
tasks up - ctrl@c1/p24, safety@c1/p23, io tasks@c0
```

The FSM then runs `BOOT` → `SELFTEST` → `DISARMED` (or `ZONE_TOUR` first, if
`boot.tour` is on — default off). See
[`diagrams/aim-states.md`](./diagrams/aim-states.md) for the full state
diagram and trigger list, and [§8](#8-known-gaps) for what `SELFTEST`
currently checks.

`input.channel` boots at `NONE` on a clean start. After a
watchdog-triggered reset it is also forced back to `NONE`, even if it was
armed before the reset — see [`architecture.md` §7](./architecture.md#7-safety).

## 4. OLED reference

Rendered at 10 Hz (100 ms) once the OLED probe succeeds at boot. If the OLED
is absent or NAKs, `ui` logs a warning once and disables rendering — the
control loop keeps running regardless (see
[`interfaces.md` §3](./interfaces.md#3-i²c0--oled)).

| Line | Example | Meaning |
|---|---|---|
| 1 | `AIM  DISARMED` | Current FSM state |
| 2 | `CH: NONE` | Active input channel (`NONE`/`AUTO_POSITIONAL`/`MANUAL`/`AUTO_VELOCITYEQUATION`) |
| 3 | `CRC:0 OVL:0` | `bad_crc`, `overlong` frame counters |
| 4 | `UNP:0 OOR:0` | `unparsed`, `out_of_range` frame counters |
| 5 | `DRPi:0 ERR:0` | `drop_inact` (inactive-channel drops), `uart_err` |
| 6 | `UP: 12s` | Uptime, seconds since boot |
| 7 | `SD:PM OK 512M` | SD: `present`/`mounted` flags, status (`OK`/`E<n>`/`FULL`), free space |

A fresh board with no SD card reads `SD:-- OK 0M` until a card is inserted
(mount is retried on a timer — see
[`interfaces.md` §4](./interfaces.md#4-spi2--sd-card)).

## 5. Status LED reference

One WS2812 (`STATUS_LED`, GPIO48). Colour follows FSM state; a channel change
from the `MODE` button briefly overrides it with a blink count
(`config::LED_*` values in `Config.hpp`):

| State | Colour | RGB |
|---|---|---|
| `BOOT` / `SELFTEST` | dim white | `(2,2,2)` |
| `ZONE_TOUR` | violet | `(20,0,24)` |
| `DISARMED` | blue | `(0,0,24)` |
| `ARMED` | green | `(0,24,0)` |
| `LINK_LOST` | amber | `(32,12,0)` |
| `PARKED` | dim white | `(2,2,2)` |
| `FAULT` | red | `(40,0,0)` |

**`MODE` button blink** (channel confirmation, overrides the state colour for
~1.5 s): the LED blinks in the new channel's colour — dim white for `NONE`
(no blink), green for `AUTO_POSITIONAL` (2 phases), blue for `MANUAL`
(4 phases) — then returns to the state colour above.

## 6. Connecting `EYE`

With the board on `DISARMED` and the `UART1` adapter connected, start `EYE`
as in the [root Quick Start](../README.md#quick-start), adding `--echo` to
see every line sent and received — a working link shows `cfg` and `tlm`
traffic immediately.

Before wiring up the camera, `eye/camera/README.md` has three link-only
checks (constant error, sweep, fire) under **Bring-up, before connecting the
camera**, plus how to set `input.channel = AUTO_POSITIONAL` and arm — see
[**Making the PC the active channel**](../eye/camera/README.md#making-the-pc-the-active-channel)
and [**Arming**](../eye/camera/README.md#arming).

## 7. First-run checklist

- [ ] `pio device monitor` shows `boot` → `selftest.ok` → `disarmed` (or
      `zone_tour` → `disarmed`) with no `FAULT`.
- [ ] OLED (if present) shows `AIM DISARMED`, `CH: NONE`, all counters `0`.
- [ ] Status LED reads blue (`DISARMED`).
- [ ] `MODE` button short-press cycles the channel; OLED `CH:` line and LED
      blink count change accordingly, with no `EYE` connected.
- [ ] `BTN_ESTOP` press latches `FAULT` (red LED); the `CONTROL` button
      acknowledges it back to `DISARMED`.
- [ ] With `EYE` connected and `--echo`: `cfg.set input.channel AUTO_POSITIONAL`
      gets a `cfg.state ... OK` reply, and `CH:` on the OLED updates to
      `AUTO_POSITIONAL`.
- [ ] Arm (`CONTROL` button or `serial_link.py --arm`); `st:` in the
      `EYE` telemetry readout reads `ARMED`, LED turns green.
- [ ] Unplug the `UART1` adapter: within 300 ms the board reports
      `LINK_LOST` (amber LED), and the `MODE` button still cycles channels
      with no link connected.

## 8. Known gaps

- **`SELFTEST` is a stub** — `Ctrl::selfTest()` always returns `true`. The
  real gate (I²C scan, SD mount, servo micro-sweep, camera handshake) is
  Phase 0 task 10 (reliability hardening), not yet implemented.
- **No wall clock in Phase 0** — `UP:` and every log timestamp are
  monotonic since boot; there is no calibration step here for that (see
  [`architecture.md` §5](./architecture.md#5-configuration-and-storage)).
- **Logic-analyser capture of the `SCOPE` pulse** (§8.3 of
  `architecture.md`) is a bench step, not part of this bringup.
