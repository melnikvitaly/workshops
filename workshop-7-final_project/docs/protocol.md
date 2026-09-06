# Protocol — `EYE` ⟷ `AIM` over UART1

The complete wire contract for both ends. `eye/camera/serial_link.py` and
`firmware/aim/src/transport/` implement this document; where any of the three
disagree, this document is right and the code is wrong.

Transport, speed and failure behaviour are in
[`interfaces.md`](./interfaces.md#2-uart1--the-eye-link). The design reasoning is
in [`architecture.md`](./architecture.md#3-communications).

---

## 1. Two traffic classes on one wire

| Class | Rate | Format | Protected by |
|---|---|---|---|
| **Control** — the error vector and the tuning console | per camera frame | compact ASCII, one line | strict grammar + range checks |
| **Config, commands, telemetry** | on demand / `telemetry.rate_hz` | **NDJSON**, one object per line | strict grammar + range checks + **CRC-8** |

Both classes share the receiver, the 256-byte line buffer and the counters. They
are told apart by the first byte of the line: `{` starts an NDJSON object,
anything else is control ASCII.

### Why control lines carry no CRC

A corrupted `E` frame is one bad sample in a stream of thirty per second. It is
caught by the range check, discarded, and superseded 20 ms later — a CRC would
add nothing the range check does not already do, and would cost bytes on the one
path where the byte budget matters.

A corrupted **config** line is different: it is persisted to NVS, it changes the
input channel or a gain, and it survives until something overwrites it. There is
no next sample to correct it. That asymmetry — not the value of the bytes — is
why the CRC sits on NDJSON and nowhere else.

`F` (fire) is the one control-path message with a physical consequence, and it is
defended by grammar rather than by a checksum: see §2.2.

---

## 2. Control path — ASCII

One frame per line, terminated by `\n`. A preceding `\r` is accepted and ignored.
All frames are case-insensitive on the tag. Fields are separated by one or more
spaces or tabs. Floats may use any format `strtof` accepts (`-0.124`, `.5`,
`1e-2`); a decimal comma is never accepted.

### 2.1 Frames

| Direction | Frame | Meaning |
|---|---|---|
| `EYE` → `AIM` | `E <dx> <dy> <valid>` | Tracking error, streamed |
| `EYE` → `AIM` | `F` | Fire one shot |
| `EYE` → `AIM` | `K <axis> <kp> <ki> <kd>` | Set PID gains live; `axis` = `p` \| `t` \| `b` |
| `EYE` → `AIM` | `N <dpan> <dtilt>` | Open-loop nudge, in degrees |
| `EYE` → `AIM` | `T <0\|1>` | Telemetry stream off / on |
| `EYE` → `AIM` | `Q` | Query gains and state |
| `AIM` → `EYE` | `G pan <kp> <ki> <kd> tilt <kp> <ki> <kd> armed <0\|1>` | Reply to `K` and `Q` |

`E` carries the error vector normalised to `[-1, 1]`, defined as
`error = target_position − laser_dot_position`, with `valid = 1` only when both
the dot and the target were seen in that frame. `N` displaces the gimbal without
telling the controller, which makes it a repeatable open-loop disturbance — the
only honest way to compare two gain sets.

Per-frame telemetry has moved to NDJSON (§3.4); the `T` frame now only toggles the
stream on and off.

### 2.2 Grammar hardening — the `F` rule

On UART1 the console no longer shares the wire, so stray `ESP_LOG` text is not
expected. The grammar is still strict, because "not expected" is not "impossible"
and the consequence here is a laser:

- A frame tag is recognised **only** as the first non-whitespace byte of a line
  that began at a newline boundary. A tag found mid-line is not a tag.
- **`F` must be the entire line.** The byte after the tag must be the terminator
  — no arguments, no trailing text. `Fatal error: ...` fails on the second byte
  and is counted as `unparsed`, never as a shot.
- Every other frame must consume its exact field count. Too few fields, too many
  fields, or trailing non-whitespace rejects the whole line.
- A rejected line is discarded whole. There is no partial application of a frame.

`F` requests a shot; it does not fire one. The beam lights only if
`laserPermitted()` in `safety` agrees — `ARMED`, link fresh, WDT healthy, no
E-stop latched ([`interfaces.md` §7](./interfaces.md#7-laser-gate--the-boot-safe-requirement)).

### 2.3 Range checks ⟦4.4⟧

Applied before any value reaches the controller. A field that fails increments
`out_of_range` and rejects the line.

| Field | Accepted |
|---|---|
| `dx`, `dy` | finite, `[-1.0, 1.0]` |
| `valid` | `0` or `1` |
| `kp`, `ki`, `kd` | finite, `[0.0, 1000.0]` — negative gains invert the loop |
| `dpan`, `dtilt` | finite, `[-30.0, 30.0]` degrees |
| `axis` | `p`, `t`, `b` |

**`NaN` and `inf` are rejected explicitly**, before the bounds test — a `NaN`
reaching the PID poisons the integrator permanently, and every comparison against
`NaN` is false, so a bounds test alone lets it through. Use `isfinite()`, not a
pair of `<` / `>` comparisons.

---

## 3. Config, command and telemetry path — NDJSON

One JSON object per line, no newlines inside the object, terminated by `\n`.

### 3.1 Line format

```text
{"t":"cfg.set","k":"input.channel","v":"AUTO","id":17}*4C\n
```

| Part | Rule |
|---|---|
| Object | A single JSON object. Must start with `{` as the first byte of the line |
| Separator | One `*`, immediately after the closing `}` |
| Checksum | Exactly two uppercase hex digits |
| Terminator | `\n` |

- **Maximum line length is 256 bytes, including the terminator.** A longer line
  is discarded through to the next newline and counted as `overlong`. The buffer
  is never grown and the line is never truncated-and-parsed.
- The `*XX` suffix is **mandatory** on every NDJSON line in both directions. A
  line without one is counted as `bad_crc`, not parsed as if it were fine.
- Unknown keys inside an object are ignored, so either end may add fields without
  breaking the other. Unknown message types (`t`) are counted as `unparsed`.

> The 256-byte cap supersedes the `MAX_LINE = 96` in the `workshop-5-miniproject`
> source that `firmware/aim/` is copied from. One 256-byte buffer serves both
> traffic classes; control lines simply never approach it.

### 3.2 CRC-8

**CRC-8/ATM**, also called CRC-8/SMBUS: polynomial `0x07`, initial value `0x00`,
input not reflected, output not reflected, no final XOR.

It is computed over **the JSON object bytes only** — from the opening `{` through
the closing `}` inclusive, and excluding the `*`, the two hex digits and the
terminator. Chosen because it is eight lines of bitwise code on both ends with no
table and no library, and because the `*XX` placement is the NMEA convention, so
a human reading a capture recognises it.

```c
uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}
```

```python
def crc8(data: bytes) -> int:
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc
```

**Conformance vector** — both implementations must produce this, and a unit test
on each end asserts it:

```text
crc8(b"123456789") == 0xF4
crc8(b'{"t":"cfg.get","k":"input.channel"}') == 0x8A
```

A mismatch increments `bad_crc`, discards the line and is **not fatal** — the
sender retries or the operator repeats the command.

### 3.3 Configuration messages

The key space is defined in [`architecture.md`](./architecture.md#5-configuration-and-storage).

| `t` | Direction | Fields | Meaning |
|---|---|---|---|
| `cfg.set` | `EYE` → `AIM` | `k`, `v`, `id` | Set one key |
| `cfg.get` | `EYE` → `AIM` | `k` (omit for all), `id` | Read back |
| `cfg.reset` | `EYE` → `AIM` | `id` | Factory reset to compiled defaults |
| `cfg.state` | `AIM` → `EYE` | `k`, `v`, `id`, `ok`, `err`, `src`, `ver` | The acknowledgement |

`id` is an opaque integer echoed back in `cfg.state`, so the sender can match an
acknowledgement to its request. `src` says who caused the change — `"uart"`,
`"button"` or `"boot"` — which is how `EYE` learns what the local `MODE` button
did while the link was down. `ver` is the NVS schema version.

**Every write is validated, applied, persisted and acknowledged**, in that order.
A silently ignored change is indistinguishable from a broken one, so `cfg.state`
is emitted for *every* `cfg.set`, accepted or rejected:

```text
{"t":"cfg.set","k":"input.channel","v":"AUTO","id":17}*4C
{"t":"cfg.state","k":"input.channel","v":"AUTO","id":17,"ok":true,"err":null,"src":"uart","ver":1}*26

{"t":"cfg.set","k":"pid.pan.kp","v":-4,"id":18}*AF
{"t":"cfg.state","k":"pid.pan.kp","v":40,"id":18,"ok":false,"err":"range","src":"uart","ver":1}*9F
```

A rejection reports the **current** value in `v`, not the rejected one, so a
failed write leaves the sender's view correct rather than merely negative.

**Rejection reasons** (`err`): `range`, `type`, `unknown_key`, `readonly`,
`nvs_write`, `schema`.

### 3.4 Telemetry and events

| `t` | Direction | Rate | Meaning |
|---|---|---|---|
| `tlm` | `AIM` → `EYE` | `telemetry.rate_hz` | Control sample — state, error, velocity, angles |
| `tlm.sd` | `AIM` → `EYE` | 1 Hz | Storage health and write performance |
| `tlm.sys` | `AIM` → `EYE` | 1 Hz | Link counters, per-task CPU, heap, stack, WDT |
| `evt` | `AIM` → `EYE` | on change | State transitions, faults, storage conditions, `BOOT` |
| `estop` | either | on demand | Emergency stop — §4 |

All three are gated on the `T` frame. Each line is shown here exactly as it goes
on the wire, with its real checksum:

```text
{"t":"tlm","up":812345,"st":"ARMED","ch":"AUTO","ex":-0.031,"ey":0.012,"vp":-4.2,"vt":1.1,"pan":92.4,"tilt":78.1}*C8
{"t":"tlm.sd","up":812345,"pres":1,"mnt":1,"full":0,"free":7861248,"werr":0,"drop":3,"qd":11,"bps":4096,"lmax":214000,"lp95":9100}*25
{"t":"tlm.sys","up":812345,"link":{"bad_crc":0,"overlong":0,"unparsed":2,"oor":0,"drop_inact":17},"cpu":{"ctrl":11,"logger":4,"ui":2,"idle":80},"heap":183240,"stack_min":1840,"wdt":0}*86
```

**Wire keys are abbreviated; the telemetry names are not.** The names in
[`architecture.md` §5](./architecture.md#5-configuration-and-storage) are the
vocabulary everything else uses — the OLED, the CSV columns, the write-up. The
wire shortens them only to stay inside the cap:

| Telemetry name | Wire key | | Telemetry name | Wire key |
|---|---|---|---|---|
| `sd.present` | `pres` | | `sd.dropped_records` | `drop` |
| `sd.mounted` | `mnt` | | `sd.queue_depth` | `qd` |
| `sd.full` | `full` | | `sd.write_bytes_per_s` | `bps` |
| `sd.free_bytes` | `free` | | `sd.write_max_latency_us` | `lmax` |
| `sd.write_errors` | `werr` | | `sd.write_p95_latency_us` | `lp95` |

**Why three messages and not one.** A single object carrying all of this is 367
bytes on the wire, and the line cap is 256 ⟦5.3⟧ — one telemetry sample would be
discarded as `overlong` by its own receiver. Splitting it is also the better
design: the control sample is the only part worth sending at frame rate, and
health and system statistics change on the order of a second. As sent above the
three lines are 117, 134 and 187 bytes, leaving room for fields to be added
without revisiting the cap.

`up` is `t_mono_us`, microseconds since boot — the same monotonic clock the SD
records carry, and the field that joins the three messages into one sample.
**Phase 0 has no wall clock**, and nothing in this protocol carries one; `EYE`
timestamps the `BOOT` event against its own clock, and that one line converts the
whole session offline
([`architecture.md` §5](./architecture.md#5-configuration-and-storage)).

```text
{"t":"evt","e":"boot","up":0,"reason":"POWERON","ver":1,"wdt_resets":2}*0B
{"t":"evt","e":"state","up":1204,"from":"DISARMED","to":"ARMED","why":"btn.control"}*95
{"t":"evt","e":"sd","up":91233,"cond":"removed","present":0}*A7
{"t":"evt","e":"laser_denied","up":91240,"why":"link_stale"}*3F
```

Every state transition is logged with its trigger, and every laser denial with its
reason — both are requirements, and both are the first thing anyone reads when a
demo misbehaves.

---

## 4. Emergency stop

```text
{"t":"estop","id":42}*84
{"t":"evt","e":"estop","up":91250,"src":"uart","latched":true}*B7
```

E-stop is **not a channel and not a configuration value**. It is accepted from any
transport, in any state, regardless of `input.channel`, and it is handled by the
realtime `safety` task, which latches `FAULT`. Recovery needs an explicit operator
acknowledgement — `{"t":"cfg.set","k":"fault.ack","v":true}` or the local
`CONTROL` button.

Routing E-stop through channel selection would mean that selecting `AUTO` disables
`PILOT`'s emergency stop in Phase 1. That is a safety defect, not a design
preference, which is why the exemption is stated in the protocol rather than left
to the implementation.

---

## 5. Receiver counters

Exposed in `tlm.sys.link` and on the OLED. All are monotonic since boot; none of
them is ever fatal.

| Counter | Incremented when |
|---|---|
| `bad_crc` | NDJSON line with a wrong or missing `*XX` |
| `overlong` | Line exceeded 256 bytes; discarded to the next newline |
| `unparsed` | Unknown tag, unknown `t`, malformed JSON, wrong field count |
| `oor` | A field failed the range check in §2.3 or §3.3 |
| `drop_inact` | A valid frame arrived on a channel that is not selected |
| `uart_err` | Driver-level framing error, overrun or break |

`drop_inact` is what makes channel exclusivity observable rather than invisible:
non-selected channels are still received and counted, then dropped before the
controller.

**The fuzzer's success criterion** ([`TASKS.md`](../TASKS.md) §10): under
truncated lines, 10 kB lines, NUL bytes, binary noise, `NaN`, `1e300`, half a
frame followed by a reset and stale replays — these counters increase, `up`
never resets, and the gimbal never moves.

---

## 6. Phase 1 — MQTT

The config and telemetry classes move to MQTT unchanged: the same JSON objects
become the payloads, and the `*XX` suffix is dropped because MQTT over TCP already
carries its own integrity check.

| Topic | Payload | Retained |
|---|---|---|
| `lasergimbal/aim/config/set` | `cfg.set` object | **No** |
| `lasergimbal/aim/config/state` | `cfg.state` object | Yes, re-published on reconnect |
| `lasergimbal/aim/telemetry` | `tlm` object | No |
| `lasergimbal/aim/events` | `evt` object | No |
| `lasergimbal/aim/status` | Last Will | Yes |

**`config/set` must be non-retained.** A retained `set` is replayed by the broker
on every reconnect, which would silently overwrite whatever the operator last
chose with the `MODE` button. `config/state` is retained precisely because it
describes what *is*, not what was once asked for.
