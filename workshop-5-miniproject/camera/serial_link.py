"""COM-port link to the ESP32-S3: one frame per line.

Downlink (PC -> ESP32):

    E <dx> <dy> <valid>\\n     tracking error, streamed   e.g. E -0.124 0.058 1
    F\\n                       fire one shot
    K <p|t|b> <kp> <ki> <kd>  set PID gains live
    N <dpan> <dtilt>          nudge open loop, in degrees (a disturbance)
    T <0|1>                   telemetry stream off / on
    Q\\n                       report gains and arm state

Uplink (ESP32 -> PC), interleaved with ordinary console logging:

    A <ex> <ey>               arrived: both axes settled  -> parse_arrival
    G pan ... tilt ... armed  gains report                -> parse_gains
    T ex:.. ey:.. st:..       telemetry sample            -> parse_telemetry

The full contract is docs/uart-protocol.md; the receiving end is
src/inputs/ErrorVectorInput.hpp. The parts of the contract this module is
responsible for:

  * PC -> ESP32 only. We never wait for a reply.
  * The firmware console shares this UART, so log lines, boot banners and the
    ROM bootloader message arrive on RX. We drain and ignore them (or print them
    with echo=True) instead of letting them fill the OS buffer.
  * Keep sending at a steady rate even when detection fails -- valid=0 holds the
    gimbal still but keeps the PID integral, whereas going silent trips the
    300 ms link timeout and resets the PIDs.
  * Never send NaN/Inf: a non-finite value is downgraded to valid=0.

Standalone, for the bring-up checklist in docs/uart-protocol.md (no camera):

    py -3 serial_link.py --port COM5 --dx 0.2   # constant pan error
    py -3 serial_link.py --sweep                # slow pan/tilt sweep
    py -3 serial_link.py --fire                 # one shot, exit
    py -3 serial_link.py --list                 # what's plugged in
    py -3 serial_link.py --monitor              # listen only, send nothing

And the tuning console of docs/pid-experiments.md:

    py -3 serial_link.py --query                     # Q
    py -3 serial_link.py --gains b 40 4 0            # K b 40 4 0
    py -3 serial_link.py --telemetry 1 --nudge 8 0   # T 1 then N 8 0
    py -3 serial_link.py --console                   # type lines interactively
"""

import math
import time

# USB-serial bridges found on ESP32 boards, in order of preference. The
# DevKitC-1's UART connector is a CP2102, so that wins if several are attached;
# `None` for the product id means "any device from this vendor".
_KNOWN_BRIDGES = [
    (0x10C4, 0xEA60, "CP210x - DevKitC USB/UART"),
    (0x303A, None,   "Espressif native USB"),
    (0x1A86, None,   "CH340/CH9102"),
    (0x0403, None,   "FTDI"),
]


def parse_gains(line):
    """dict if `line` is the firmware's gain report, else None.

        G pan 40.00 4.00 0.00 tilt 35.00 6.00 0.00 armed 1

    Sent in reply to both `Q` and `K`, so a `K` is self-confirming: what comes
    back is what the firmware actually applied, not what you asked for.
    """
    t = line.split()
    if len(t) != 11 or t[0] != "G" or t[1] != "pan" or t[5] != "tilt" or t[9] != "armed":
        return None
    try:
        return {"pan": (float(t[2]), float(t[3]), float(t[4])),
                "tilt": (float(t[6]), float(t[7]), float(t[8])),
                "armed": t[10] != "0"}
    except ValueError:
        return None


def parse_telemetry(line):
    """dict if `line` is an uplink telemetry sample, else None.

        T ex:-0.124 ey:+0.058 vpan:-4.9 vtilt:+2.1 pan:57.4 tilt:88.2 st:TRACK

    The leading `T` is mandatory: console logs can contain telemetry-like text,
    but must never be mistaken for a protocol message.
    """
    tokens = line.split()
    if len(tokens) != 8 or tokens[0] != "T":
        return None
    out = {}
    for token in tokens[1:]:
        key, sep, value = token.partition(":")
        if not sep or not key or not value or key in out:
            return None
        out[key] = value
    required = {"ex", "ey", "vpan", "vtilt", "pan", "tilt", "st"}
    if set(out) != required:
        return None
    try:
        for k in ("ex", "ey", "vpan", "vtilt", "pan", "tilt"):
            out[k] = float(out[k])
    except ValueError:
        return None
    return out


def parse_arrival(line):
    """(ex, ey) if `line` is the firmware's arrival uplink, else None.

        A +0.0012 -0.0031

    Sent once, edge-triggered, the moment both axes settle inside the firmware's
    deadzone - the one thing the firmware ever sends back on this link. It is a
    bare printf rather than a decorated ESP_LOGI line, which is what lets it be
    parsed instead of merely displayed. The exactly-three-tokens test keeps
    ordinary log text ("A" appears in plenty of it) from reading as an arrival.
    """
    parts = line.split()
    if len(parts) != 3 or parts[0].upper() != "A":
        return None
    try:
        return float(parts[1]), float(parts[2])
    except ValueError:
        return None


def _describe(p):
    vid = f"{p.vid:04X}" if p.vid is not None else "----"
    pid = f"{p.pid:04X}" if p.pid is not None else "----"
    return f"{p.device:<8} {vid}:{pid}  {p.description}"


def list_ports(quiet=False):
    """(candidates, all_ports). Candidates are (rank, port, bridge name)."""
    try:
        from serial.tools import list_ports as _lp
    except ImportError:
        raise SystemExit(
            "pyserial not installed. Install it for the interpreter you're "
            "running:\n    py -3 -m pip install -r requirements.txt")
    ports = sorted(_lp.comports(), key=lambda p: p.device)
    candidates = []
    for p in ports:
        for rank, (vid, pid, name) in enumerate(_KNOWN_BRIDGES):
            if p.vid == vid and (pid is None or p.pid == pid):
                candidates.append((rank, p, name))
                break
    candidates.sort(key=lambda c: (c[0], c[1].device))
    if not quiet:
        if not ports:
            print("No serial ports at all.")
        for p in ports:
            hit = next((c for c in candidates if c[1] is p), None)
            print(("  * " if hit else "    ") + _describe(p)
                  + (f"   <- {hit[2]}" if hit else ""))
    return candidates, ports


def autodetect_port():
    """The most likely ESP32 port. Raises SystemExit if there is no good guess.

    Identification is by USB VID/PID, not by name: COM numbering is assigned by
    Windows in plug order and tells you nothing about what is on the other end.
    """
    candidates, ports = list_ports(quiet=True)
    if not candidates:
        raise SystemExit(
            "--port auto: no ESP32-style USB-serial adapter found.\n" +
            ("Ports seen:\n  " + "\n  ".join(_describe(p) for p in ports)
             if ports else "No serial ports at all - is the board plugged in?") +
            "\nPass the port explicitly with --port COMx.")
    _, port, name = candidates[0]
    if len(candidates) > 1:
        others = ", ".join(c[1].device for c in candidates[1:])
        print(f"Serial: {len(candidates)} candidates ({port.device}, {others}); "
              f"taking {port.device}. Use --port COMx to override.")
    print(f"Serial: auto-detected {port.device} ({name})")
    return port.device


class ErrorLink:
    """Sends error frames to the ESP32.

    port=None is inert (dry run); port="auto" picks the board by USB VID/PID.
    """

    def __init__(self, port=None, baud=115200, max_rate=30.0, echo=False):
        self.echo = echo
        self.min_interval = 1.0 / max_rate if max_rate > 0 else 0.0
        self.sent = 0
        self.fired = 0
        self._last = 0.0
        self._rx = b""
        self._pending = []   # complete lines received, not yet handed to poll()
        self._ser = None
        if port is None:
            self.port = None
            print("Serial: disabled (no --port) -- detection only")
            return
        if str(port).lower() == "auto":
            port = autodetect_port()
        self.port = port
        try:
            import serial  # pyserial
        except ImportError:
            raise SystemExit(
                "pyserial not installed. Install it for the interpreter you're "
                "running:\n    py -3 -m pip install -r requirements.txt")
        # Open with DTR and RTS DEASSERTED, and set them before open() so the
        # port is never brought up with them high. On a dev board those two
        # lines drive the auto-reset circuit (RTS->EN, DTR->IO0): pyserial
        # asserts both by default, which reboots the ESP32 the instant the port
        # opens and can hold it in reset for the whole session - in which case
        # the only thing ever read back is a solid stream of 0x00.
        self._ser = serial.Serial()
        self._ser.port = port
        self._ser.baudrate = baud
        self._ser.timeout = 0            # reads never block the vision loop
        self._ser.write_timeout = 0.2
        self._ser.dtr = False
        self._ser.rts = False
        self._ser.open()
        print(f"Serial: {port} @ {baud} 8N1, "
              f"frames capped at {max_rate:g} Hz")

    def send(self, dx, dy, valid):
        """Send one frame. Returns True if it went out, False if rate-limited."""
        now = time.time()
        if now - self._last < self.min_interval:
            return False
        self._last = now

        if not (math.isfinite(dx) and math.isfinite(dy)):
            dx, dy, valid = 0.0, 0.0, False
        line = f"E {dx:.4f} {dy:.4f} {1 if valid else 0}\n"

        if self._write(line):
            self.sent += 1
        self._drain()
        return True

    def fire(self):
        """Pull the trigger: one laser flash. Never rate-limited or dropped.

        A one-shot frame of its own rather than a field of the E frame -- an E
        frame is a measurement that may be dropped or superseded at will, and
        nothing that can be dropped should be able to fire the laser.
        """
        self.fired += 1
        print(f"FIRE -> {self.port or '(no port)'}")
        self._write("F\n")
        self._drain()

    # --- tuning console (docs/pid-experiments.md) ----------------------------
    # One-shot commands, never rate limited: unlike an E frame, none of them is
    # a measurement that a later one supersedes.

    def set_gains(self, axis, kp, ki, kd):
        """`K <p|t|b> <kp> <ki> <kd>` - set PID gains live, without a reflash.

        The axis letter and the negative check are validated here as well as in
        the firmware. The firmware's answer to a bad frame is to drop it and
        increment a counter you cannot see from the PC, which looks exactly like
        a working command that did nothing.
        """
        # Spelled-out names are accepted, junk is not: truncating whatever was
        # passed to its first letter would turn a typo into a valid frame for
        # the wrong axis.
        a = {"p": "p", "pan": "p", "t": "t", "tilt": "t",
             "b": "b", "both": "b"}.get(str(axis).strip().lower())
        if a is None:
            raise ValueError(f"axis must be p/pan, t/tilt or b/both (got {axis!r})")
        if min(kp, ki, kd) < 0.0:
            raise ValueError("negative gains invert the loop; the firmware "
                             "rejects them")
        self.send_raw(f"K {a} {kp:g} {ki:g} {kd:g}\n")

    def nudge(self, dpan_deg, dtilt_deg):
        """`N <dpan> <dtilt>` - displace the gimbal open loop, in degrees.

        The controller is not told, so this is a repeatable disturbance to
        reject rather than a move: the same kick every time, which is what makes
        two gain sets comparable.
        """
        self.send_raw(f"N {dpan_deg:g} {dtilt_deg:g}\n")

    def telemetry(self, on):
        """`T <0|1>` - start/stop the plottable per-frame stream.

        It shares this UART with the frames we are sending, so leave it off
        except while tuning.
        """
        self.send_raw(f"T {1 if on else 0}\n")

    def query(self):
        """`Q` - ask for the current gains; the firmware replies with `G ...`."""
        self.send_raw("Q\n")

    def send_raw(self, line):
        print(f"-> {line.strip()}")
        self._write(line)
        self._drain()

    def _write(self, line):
        if self._ser is None:
            return False
        try:
            self._ser.write(line.encode("ascii"))
            return True
        except Exception as e:   # a USB hiccup must not kill the vision loop
            print("Serial write failed:", e)
            return False

    def poll(self):
        """Whatever the firmware has said since the last call, as text lines.

        The link is PC -> ESP32 only, but the firmware console shares this UART,
        so ESP_LOGI output, boot banners, and panics also arrive here. Reading
        them costs nothing and leaving them unread eventually fills the OS
        buffer, so this runs every frame whether anyone looks or not.
        """
        self._drain()
        lines, self._pending = self._pending, []
        return lines

    def _drain(self):
        if self._ser is None:
            return
        try:
            waiting = self._ser.in_waiting
            if not waiting:
                return
            data = self._ser.read(waiting)
        except Exception:
            return

        self._rx += data
        while b"\n" in self._rx:
            raw, self._rx = self._rx.split(b"\n", 1)
            # NULs are stripped rather than shown: a board held in reset streams
            # solid 0x00, and that should not turn into pages of garbage.
            text = raw.decode("utf-8", "replace").replace("\x00", "").strip()
            if not text:
                continue
            self._pending.append(text)
            if self.echo:
                print("  esp32 |", text)
        # A partial line is normal; an unbounded one is not (a stuck-low RX line
        # never delivers a '\n' at all).
        if len(self._rx) > 4096:
            self._rx = self._rx[-512:]
        del self._pending[:-200]        # cap if nobody is calling poll()

    def close(self, park=True):
        if self._ser is not None:
            try:
                if park:
                    self.send_now(0.0, 0.0, False)  # gimbal stops, PIDs kept
                self._ser.close()
            except Exception:
                pass
            self._ser = None

    def send_now(self, dx, dy, valid):
        """send() ignoring the rate limit."""
        self._last = 0.0
        return self.send(dx, dy, valid)


def _describe_reply(line):
    """One incoming line, rendered for a human. Falls back to the raw text."""
    gains = parse_gains(line)
    if gains is not None:
        p, t = gains["pan"], gains["tilt"]
        return (f"gains: pan kp={p[0]:g} ki={p[1]:g} kd={p[2]:g} | "
                f"tilt kp={t[0]:g} ki={t[1]:g} kd={t[2]:g} | "
                f"{'ARMED' if gains['armed'] else 'DISARMED'}")
    arrival = parse_arrival(line)
    if arrival is not None:
        return f"ARRIVED on target, residual {arrival[0]:+.4f} {arrival[1]:+.4f}"
    tel = parse_telemetry(line)
    if tel is not None:
        return (f"ex:{tel['ex']:+.3f} ey:{tel.get('ey', 0.0):+.3f} "
                f"pan:{tel.get('pan', 0.0):.1f} tilt:{tel.get('tilt', 0.0):.1f} "
                f"[{tel['st']}]")
    return line


def _print_replies(link, seconds):
    """Drain and pretty-print for `seconds`. Returns the lines seen."""
    seen = []
    deadline = time.time() + seconds
    while time.time() < deadline:
        for line in link.poll():
            seen.append(line)
            print("  " + _describe_reply(line))
        time.sleep(0.02)
    if not seen:
        print("  (no reply - is the firmware running? try --monitor)")
    return seen


def _console(link):
    """Type protocol lines, watch the replies. The workflow of pid-experiments.

    Reading the port happens on a background thread so replies appear while you
    are still typing, rather than only after the next command.
    """
    import threading

    print("Console. Type a protocol line and press Enter; blank line = poll only.\n"
          "  Q                 current gains\n"
          "  K b 40 4 0        set gains (axis p | t | b)\n"
          "  N 8 0             nudge 8 degrees of pan, open loop\n"
          "  T 1 / T 0         telemetry on / off\n"
          "  F                 fire\n"
          "  E 0.2 0 1         one error frame\n"
          "Ctrl-C or 'exit' to leave.")

    stop = threading.Event()

    def reader():
        while not stop.is_set():
            for line in link.poll():
                print("  " + _describe_reply(line))
            time.sleep(0.05)

    t = threading.Thread(target=reader, daemon=True)
    t.start()
    try:
        while True:
            text = input("> ").strip()
            if text.lower() in ("exit", "quit"):
                break
            if text:
                link.send_raw(text.rstrip("\n") + "\n")
    except (KeyboardInterrupt, EOFError):
        pass
    finally:
        stop.set()
        t.join(timeout=1.0)
        link.close(park=False)


def _main():
    import argparse

    ap = argparse.ArgumentParser(
        description="Send error frames by hand (bring-up / axis direction check)")
    ap.add_argument("--port", default="auto",
                    help="COM port, e.g. COM5, or 'auto' (default) to find the "
                         "board by USB VID/PID")
    ap.add_argument("--list", action="store_true",
                    help="list the serial ports, marking the likely board, and exit")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--rate", type=float, default=20.0, help="frames per second")
    ap.add_argument("--dx", type=float, default=0.0)
    ap.add_argument("--dy", type=float, default=0.0)
    ap.add_argument("--sweep", action="store_true",
                    help="sweep dx/dy through a slow circle instead of holding "
                         "the --dx/--dy constants")
    ap.add_argument("--fire", action="store_true",
                    help="send a single 'F' (laser flash) and exit")
    ap.add_argument("--monitor", action="store_true",
                    help="only listen: print what the ESP32 says, send nothing")
    ap.add_argument("--echo", action="store_true", help="print firmware logs")

    # --- tuning console, see docs/pid-experiments.md ---
    ap.add_argument("--query", action="store_true",
                    help="Q: print the current gains and arm state, then exit")
    ap.add_argument("--gains", nargs=4, metavar=("AXIS", "KP", "KI", "KD"),
                    help="K: set gains live, AXIS = p | t | b, e.g. --gains b 40 4 0")
    ap.add_argument("--nudge", nargs=2, type=float, metavar=("DPAN", "DTILT"),
                    help="N: displace the gimbal open loop, in degrees - a "
                         "repeatable disturbance for the loop to reject")
    ap.add_argument("--telemetry", type=int, choices=[0, 1], metavar="0|1",
                    help="T: start/stop the plottable per-frame stream")
    ap.add_argument("--console", action="store_true",
                    help="interactive: type protocol lines (Q, K b 40 4 0, N 8 0, "
                         "T 1, F, E ...) and watch the replies")
    args = ap.parse_args()

    if args.list:
        list_ports()
        return

    # Any of these is a one-shot exchange: send, give the firmware a moment to
    # answer, print whatever came back. They compose, so `--gains ... --nudge ...`
    # runs a whole experiment in one line.
    if args.query or args.gains or args.nudge or args.telemetry is not None:
        link = ErrorLink(args.port, args.baud, echo=False)
        try:
            if args.gains:
                axis, kp, ki, kd = args.gains
                try:
                    link.set_gains(axis, float(kp), float(ki), float(kd))
                except ValueError as e:
                    raise SystemExit(f"--gains: {e}")
            if args.telemetry is not None:
                link.telemetry(args.telemetry)
            if args.nudge:
                link.nudge(*args.nudge)
            if args.query:
                link.query()
            _print_replies(link, 0.4)
        finally:
            link.close(park=False)       # sent no E frames; nothing to park
        return

    if args.console:
        _console(ErrorLink(args.port, args.baud, echo=True))
        return

    if args.monitor:
        # Pure listener - the gimbal never moves, so this is the safe way to
        # check the wiring and see the firmware's banner and telemetry.
        link = ErrorLink(args.port, args.baud, echo=True)
        print("Monitoring. Nothing is sent. Ctrl-C to stop.")
        try:
            while True:
                link.poll()
                time.sleep(0.05)
        except KeyboardInterrupt:
            pass
        finally:
            link.close(park=False)
        return

    link = ErrorLink(args.port, args.baud, max_rate=args.rate * 2, echo=args.echo)
    if args.fire:
        link.fire()
        time.sleep(0.1)          # let the byte leave before closing the port
        link.close()
        return

    print("Ctrl-C to stop (a final valid=0 frame parks the gimbal).")
    t0 = time.time()
    try:
        while True:
            if args.sweep:
                t = (time.time() - t0) * 0.5          # ~1 revolution / 12 s
                dx, dy = 0.3 * math.cos(t), 0.3 * math.sin(t)
            else:
                dx, dy = args.dx, args.dy
            link.send_now(dx, dy, True)
            time.sleep(1.0 / args.rate)
    except KeyboardInterrupt:
        pass
    finally:
        link.close()


if __name__ == "__main__":
    _main()
