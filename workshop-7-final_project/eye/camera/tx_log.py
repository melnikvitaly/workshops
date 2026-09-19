"""Single place every outgoing ESP32 command line passes through.

ErrorLink (serial_link.py) already has one low-level write path -- _write()
-- so this hooks there instead of duplicating a print/log call at each of the
half-dozen higher-level senders (send, manual, fire, send_raw -- and
set_gains/nudge/telemetry/query/cfg_set/set_channel/press_control all go
through send_raw). Before this module existed, fire() and send_raw() each
carried their own ad-hoc print, and the streamed E/M frames (send()/manual())
were not logged at all: three different behaviours for what is really one
thing, a line written to the wire.

Two independent outputs, both optional:

  * console  -- one line per send, EXCEPT an `E` frame without --echo. `E` is
                the one tag sent continuously regardless of any user action
                -- up to the link's max rate (commonly 20-30 Hz) for as long
                as AUTO is running -- so printing every one by default would
                drown everything else; it stays quiet unless explicitly
                asked for. Every other tag prints unconditionally, `M`
                (manual_control.py's keyboard drive) included: unlike `E`, an
                `M` frame only goes out while an operator is actively holding
                a direction key, the same deliberate-action shape as `F` or
                `N`, so it gets the same always-visible treatment.
  * file     -- every single line, tag included, one per row, timestamped
                with elapsed seconds since the log was opened. Nothing is
                filtered out of it regardless of --echo -- it is meant for
                offline analysis (soak runs, replay), where a gap in the
                record is worse than a large file.

Logging happens whether or not a line actually reached the wire (dry run,
port=None): this is a record of what ErrorLink was asked to send, matching
send_raw()'s old behaviour of printing before it even called _write().
"""

import time


class TxLog:
    """Logs every line ErrorLink writes to the ESP32. See module docstring."""

    # 'E' is the only tag sent continuously regardless of user action --
    # quiet on the console unless --echo asks for it too. Everything else,
    # 'M' included, is a deliberate action worth seeing every time.
    _STREAMED_TAGS = ("E",)

    def __init__(self, path=None, echo=False):
        self.echo = echo
        self.count = 0
        self._t0 = time.monotonic()
        self._fh = None
        if path:
            self._fh = open(path, "a", encoding="utf-8", newline="\n")
            self._fh.write(f"# tx log opened {time.strftime('%Y-%m-%d %H:%M:%S')}\n")
            self._fh.flush()
            print(f"Serial: logging sent commands to {path}")

    def record(self, line):
        """Log one outgoing line, with or without its trailing newline."""
        text = line.rstrip("\n")
        if not text:
            return
        self.count += 1
        tag = text[0].upper()

        if self._fh is not None:
            self._fh.write(f"{time.monotonic() - self._t0:9.3f} {text}\n")
            self._fh.flush()

        if self.echo or tag not in self._STREAMED_TAGS:
            print(f"-> {text}")

    def close(self):
        if self._fh is not None:
            try:
                self._fh.close()
            except Exception:
                pass
            self._fh = None
