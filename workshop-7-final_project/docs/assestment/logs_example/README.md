# Example logs

Trimmed console captures, kept to show the log shape and format — not raw,
unedited dumps. Repetitive runs (identical stat lines, identical zero-error
frames) are cut down to a couple of examples with a
`[... N lines elided ...]` marker; state transitions and one-off events are
kept in full.

## Files

| File | Source | Contents |
|------|--------|----------|
| `esp.log` | `AIM` (ESP32-S3) console, over the programming USB port | `ESP_LOG` output: periodic CPU%/stack stats, FSM transitions, config-plane events |
| `tracker.log` | `tracker.py` (`EYE`) stdout, over the UART1 data link | Console output: OAK pipeline status, sent frames (`--echo`), received telemetry, an arm and a channel-switch round trip |

`esp.log` and `tracker.log` are from two different capture sessions (the
`AIM` board was reflashed between them, resetting its uptime clock), so their
timestamps don't line up — each demonstrates its own format, not a single
synchronized run.

## How they were captured

Two terminals, both from the repo root
([`firmware/aim/`](../../../firmware/aim) and
[`eye/camera/`](../../../eye/camera)):

```powershell
# Terminal 1 — AIM console (adjust -p to your board's console port)
cd firmware/aim
pio device monitor -p COM10 -b 115200 |
  Tee-Object -FilePath ..\..\docs\assestment\logs_example\esp.log -Encoding utf8
```

```powershell
# Terminal 2 — EYE console (adjust --port, or use `auto`)
cd eye/camera
$env:PYTHONUNBUFFERED = 1
py -3 tracker.py --port auto --echo --verbose 2>&1 |
  Tee-Object -FilePath ..\..\docs\assestment\logs_example\tracker.log -Encoding utf8
```

Do not point both terminals at the same COM port: on this host `COM6` is the
UART1 data-link adapter (`tracker.py`'s own `Q`/`G` auto-detect confirms it),
not the console. Pointing `pio device monitor` at it captures raw NDJSON
telemetry instead of the ESP-IDF console log.

With both terminals running and the `EYE` window focused, click
**Arm / Disarm (CONTROL)** in the controls panel — the standard action, and
the UI's mirror of the board's physical `CONTROL` button. Let it run a few
seconds, then `Ctrl+C` both terminals (or `q` in the `EYE` window first).

## Notes

- COM port numbers are host-specific; the `AIM` console and the UART1 link
  are two separate USB-serial adapters (see
  [`docs/interfaces.md`](../../interfaces.md) §1–2).
- `tracker.py --port auto` finds the UART1 adapter itself via the `Q`/`G`
  handshake in [`serial_link.py`](../../../eye/camera/serial_link.py), so it
  does not need the right COM number guessed up front.
- `Tee-Object` with no `-Encoding` writes UTF-16LE in Windows PowerShell 5.1;
  both files were converted to plain UTF-8 after capture. Pass
  `-Encoding utf8` yourself when recapturing.
- The raw `esp.log` capture opened with several thousand lines of the exact
  same `cpu idle ...` text, some with a doubled `I (I (...)` prefix, before
  settling into normal output. That is a `pio device monitor` / `Tee-Object`
  redirection artifact (no real terminal on the receiving end), not real
  device behaviour — it has been cut from this file.
