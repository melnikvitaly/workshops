"""COM-port link to the ESP32-S3: one frame per line.

Downlink (PC -> ESP32):

    E <dx> <dy> <valid>\\n     tracking error, streamed   e.g. E -0.124 0.058 1
    M <vpan> <vtilt>\\n        direct velocity, deg/s, streamed  (MANUAL channel)
    F\\n                       fire one shot
    K <p|t|b> <kp> <ki> <kd>  set PID gains live
    N <dpan> <dtilt>          nudge open loop, in degrees (a disturbance)
    P <pan> <tilt>            absolute position, in degrees -> move_to/center
    T <0|1>                   telemetry stream off / on
    Q\\n                       report gains and arm state
    {"t":"cfg.set",...}*XX    NDJSON config write, e.g. input.channel -> cfg_set/set_channel
                               control.press is a remote CONTROL-button press -> press_control
                               zone.{pan,tilt}.{min,max} -> set_zone
                               control.zone_tour restarts ZONE_TOUR on demand -> zone_tour
                               boot.tour (tour after SELFTEST, NVS) -> set_boot_tour

Uplink (ESP32 -> PC):

    G pan ... tilt ... armed  gains report                -> parse_gains
    {"t":"tlm",...}*XX        NDJSON control sample        -> parse_tlm
    {"t":"cfg.state",...}*XX  ack for cfg.set/cfg.get      -> parse_cfg_state

Console logging (ESP_LOGI, boot banner, ROM bootloader messages, panics) is
UART0 / USB-Serial-JTAG, not this port -- see `sdkconfig.esp32-s3-devkitc-1`
(CONFIG_ESP_CONSOLE_UART_NUM=0) and firmware/aim/src/Pinout.hpp (LINK_UART =
UART_NUM_1). This UART carries only the control-ASCII frames above and NDJSON
config/telemetry/event lines (docs/protocol.md); nothing writes raw log text
to it (the only write path is UartTransport::writeLine()).

parse_telemetry() below parses an older per-frame ASCII `T ex:.. ...` uplink
format that firmware/aim no longer sends -- current telemetry is the NDJSON
`{"t":"tlm",...}` line in docs/protocol.md §3.4. Kept for reference only.

The receiving end is firmware/aim/src/transport/Protocol.hpp (control ASCII)
and firmware/aim/src/tasks/LinkUart.hpp (NDJSON + `G` reply). The parts of the
contract this module is responsible for:

  * PC -> ESP32 only for the control path. We never wait for a reply, except
    in the explicit query/gains/nudge/telemetry commands below and the
    handshake `autodetect_port()` uses to confirm the port.
  * Still drain every line, even lines this module does not otherwise use --
    leaving RX unread eventually fills the OS buffer, and echo=True is how a
    human watches it.
  * Keep sending at a steady rate even when detection fails -- valid=0 holds the
    gimbal still but keeps the PID integral, whereas going silent trips the
    300 ms link timeout and resets the PIDs.
  * Never send NaN/Inf: a non-finite value is downgraded to valid=0.

Standalone, for bring-up (no camera):

    py -3 serial_link.py --port COM5 --dx 0.2   # constant pan error
    py -3 serial_link.py --sweep                # slow pan/tilt sweep
    py -3 serial_link.py --manual 20 0          # one M frame, MANUAL channel
    py -3 serial_link.py --fire                 # one shot, exit
    py -3 serial_link.py --list                 # what's plugged in
    py -3 serial_link.py --monitor              # listen only, send nothing

And the tuning console:

    py -3 serial_link.py --query                     # Q
    py -3 serial_link.py --gains b 40 4 0            # K b 40 4 0
    py -3 serial_link.py --telemetry 1 --nudge 8 0   # T 1 then N 8 0
    py -3 serial_link.py --channel AUTO              # cfg.set input.channel AUTO
    py -3 serial_link.py --control                   # cfg.set control.press (arm/disarm toggle)
    py -3 serial_link.py --zone 40 110 80 120        # cfg.set zone.{pan,tilt}.{min,max}
    py -3 serial_link.py --zone-tour                 # cfg.set control.zone_tour
    py -3 serial_link.py --boot-tour on              # cfg.set boot.tour (tour after SELFTEST)
    py -3 serial_link.py --zone-limits                # cfg.get zone.limit.* (mechanical travel)
    py -3 serial_link.py --move-to 75 100             # P 75 100 (absolute degrees)
    py -3 serial_link.py --center                     # move to the current working zone's centre
    py -3 serial_link.py --console                   # type lines interactively
"""

import collections
import json
import math
import queue
import threading
import time

from tx_log import TxLog

# Quiet time on the E/M stream before the keepalive thread steps in. Well under
# the firmware's 300 ms link timeout, well over one frame interval (33 ms at 30 Hz).
KEEPALIVE_AFTER = 0.12

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

        T ex:-0.124 ey:+0.058 vpan:-4.9 vtilt:+2.1 pan:57.4 tilt:88.2 st:TRACK arr:1

    This is the retired per-frame ASCII telemetry format. firmware/aim now
    sends telemetry as the NDJSON `tlm` line (docs/protocol.md §3.4), which
    this function does not parse -- it always returns None against current
    firmware. Kept for reference / older firmware only.
    """
    tokens = line.split()
    if len(tokens) != 9 or tokens[0] != "T":
        return None
    out = {}
    for token in tokens[1:]:
        key, sep, value = token.partition(":")
        if not sep or not key or not value or key in out:
            return None
        out[key] = value
    required = {"ex", "ey", "vpan", "vtilt", "pan", "tilt", "st", "arr"}
    if set(out) != required:
        return None
    try:
        for k in ("ex", "ey", "vpan", "vtilt", "pan", "tilt"):
            out[k] = float(out[k])
        if out["arr"] not in ("0", "1"):
            return None
        out["arr"] = out["arr"] == "1"
    except ValueError:
        return None
    return out


def crc8(data):
    """CRC-8/ATM (poly 0x07, init 0x00) over `data` -- docs/protocol.md §3.2.

    Same algorithm as firmware/aim/src/utils/Crc8.hpp; both ends must agree,
    so this is the reference Python implementation quoted in the protocol doc.
    """
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def parse_ndjson(line):
    """The parsed object if `line` is a well-formed, checksum-valid NDJSON
    line, else None.

        {"t":"tlm","up":812345,...}*C8

    Covers any message type (tlm, tlm.sd, tlm.sys, evt, cfg.state, ...) --
    callers filter on the `t` field. A missing/wrong `*XX` or invalid JSON is
    silently None: the firmware already counts these as bad_crc/unparsed,
    this is just the PC side agreeing rather than raising.
    """
    if not line.startswith("{"):
        return None
    body, sep, tail = line.rpartition("*")
    if not sep or len(tail) != 2:
        return None
    try:
        want = int(tail, 16)
    except ValueError:
        return None
    if crc8(body.encode("utf-8")) != want:
        return None
    try:
        obj = json.loads(body)
    except ValueError:
        return None
    return obj if isinstance(obj, dict) else None


def parse_tlm(line):
    """dict if `line` is a `tlm` control sample (docs/protocol.md §3.4), else
    None.

        {"t":"tlm","up":..,"st":"ARMED","ch":"AUTO","ex":..,"ey":..,
         "vp":..,"vt":..,"pan":..,"tilt":..}*XX

    Wire keys are kept as-is (ex/ey/vp/vt/pan/tilt) -- see architecture.md §5
    for the un-abbreviated telemetry names. This is the current uplink
    telemetry format; parse_telemetry() above is the retired one.
    """
    obj = parse_ndjson(line)
    if obj is None or obj.get("t") != "tlm":
        return None
    return obj


def parse_cfg_state(line):
    """dict if `line` is a `cfg.state` acknowledgement, else None.

        {"t":"cfg.state","k":"input.channel","v":"AUTO","id":17,"ok":true,
         "err":null,"src":"uart","ver":1}*XX

    docs/protocol.md §3.3 -- the reply to every cfg.set, accepted (`ok`:
    true, `v` is the applied value) or rejected (`ok`: false, `v` is the
    value that stayed in effect, `err` names why).
    """
    obj = parse_ndjson(line)
    if obj is None or obj.get("t") != "cfg.state":
        return None
    return obj


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


def _handshake_ok(port_device, baud, timeout=2.5, resend_interval=0.4):
    """True if `port_device` answers the `Q` -> `G ...` exchange (§2.1, protocol.md).

    VID/PID (see `_KNOWN_BRIDGES`) only says "this is a USB-serial bridge of a
    kind our boards use" -- it does not prove AIM firmware is on the other end,
    or that the adapter is even wired to UART1 rather than someone's unrelated
    project. `Q` is answered unconditionally by `LinkUart::emitGains()` in any
    firmware state, so a `G` reply is the one cheap proof that actually matters.

    `timeout` is generous on purpose. Setting dtr/rts False before open() (see
    ErrorLink.__init__) stops us from *holding* the board in reset, but does not
    stop a brief edge on those lines the moment Windows opens the handle -- on
    an RC auto-reset circuit that edge alone is enough to reboot the ESP32-S3,
    and a full boot (NVS load, display init in UiTask::init(), task startup)
    can take over a second before LinkUart is alive to answer. `Q` is resent
    every `resend_interval` while waiting, so a first attempt lost to that
    reboot -- or to the RX buffer not existing yet -- isn't fatal to the probe.
    """
    try:
        import serial
    except ImportError:
        return False
    ser = serial.Serial()
    ser.port = port_device
    ser.baudrate = baud
    ser.timeout = 0.05
    ser.write_timeout = 0.2
    # Same reasoning as ErrorLink.__init__: deasserted before open() so probing
    # a port never *holds* the board in reset via the RTS/DTR auto-reset circuit.
    ser.dtr = False
    ser.rts = False
    try:
        ser.open()
    except Exception:
        return False
    try:
        try:
            ser.reset_input_buffer()
        except Exception:
            pass
        buf = b""
        now = time.time()
        deadline = now + timeout
        next_send = now
        while time.time() < deadline:
            if time.time() >= next_send:
                try:
                    ser.write(b"Q\n")
                except Exception:
                    pass
                next_send = time.time() + resend_interval
            chunk = ser.read(256)
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                text = raw.decode("utf-8", "replace").replace("\x00", "").strip()
                if text and parse_gains(text) is not None:
                    return True
        return False
    except Exception:
        return False
    finally:
        try:
            ser.close()
        except Exception:
            pass


def autodetect_port(baud=115200, handshake_timeout=2.5):
    """The AIM board's port, confirmed live. Raises SystemExit if none answers.

    Candidates are found by USB VID/PID (COM numbering is assigned by Windows
    in plug order and says nothing about what's attached), then tried in rank
    order with the `Q`/`G` handshake (`_handshake_ok`) -- the port that talks
    back as AIM firmware wins, not just the first thing that looks like an
    ESP32 USB-serial bridge. `handshake_timeout` defaults high enough to ride
    out a possible reboot-on-open plus a full boot; see `_handshake_ok`.
    """
    candidates, ports = list_ports(quiet=True)
    if not candidates:
        raise SystemExit(
            "--port auto: no ESP32-style USB-serial adapter found.\n" +
            ("Ports seen:\n  " + "\n  ".join(_describe(p) for p in ports)
             if ports else "No serial ports at all - is the board plugged in?") +
            "\nPass the port explicitly with --port COMx.")
    tried = []
    for _, port, name in candidates:
        print(f"Serial: probing {port.device} ({name}) for the Q/G handshake "
              f"(up to {handshake_timeout:g}s, covers a reboot) ...")
        if _handshake_ok(port.device, baud, handshake_timeout):
            print(f"Serial: auto-detected {port.device} ({name}) -- confirmed by handshake")
            return port.device
        tried.append(f"{port.device} ({name})")
    raise SystemExit(
        "--port auto: found USB-serial adapter(s) " + ", ".join(tried) +
        " but none answered the Q/G handshake.\n"
        "Is AIM firmware flashed and running, and is this adapter wired to "
        "UART1 (GPIO17/18), not the USB-CDC console?\n"
        "Pass the port explicitly with --port COMx, or use --list to see what's plugged in.")


class ErrorLink:
    """Sends error frames to the ESP32.

    port=None is inert (dry run); port="auto" finds the board by USB VID/PID
    and confirms it with the `Q`/`G` handshake (see `autodetect_port`).
    """

    def set_rate(self, max_rate):
        """Cap on frames/second put on the wire; 0 = no cap. Safe to call live."""
        self.min_interval = 1.0 / max_rate if max_rate > 0 else 0.0

    def __init__(self, port=None, baud=115200, max_rate=30.0, echo=False,
                 tx_log_path=None):
        self.echo = echo
        self.set_rate(max_rate)
        self.sent = 0
        self.fired = 0
        # Every line _write() sends passes through here -- see tx_log.py for
        # why this is one call site instead of one print per sender.
        self._tx_log = TxLog(tx_log_path, echo=echo)
        # Last on/off requested through telemetry() -- not the firmware's
        # actual state, which we have no way to read back. A caller that
        # retries T 1 to survive a lost request (e.g. detect_dots.py, see its
        # run()) checks this first, so it never re-enables telemetry an
        # operator explicitly turned off with T 0.
        self.telemetry_wanted = True
        self._cfg_id = 0     # id counter for cfg.set/cfg.get, matched in cfg.state
        # Last channel asked for through set_channel() (None = never). Lets
        # manual_control.py stop streaming `M` as soon as the operator picks
        # another channel, without waiting for a tlm sample to confirm it.
        self.channel_requested = None
        self._last = 0.0
        self._last_manual = 0.0
        self._rx = b""       # worker only: partial line being assembled
        # Complete lines received, not yet handed to poll(). A deque so the
        # worker appends and poll() pops with no lock; maxlen drops the oldest
        # if nobody is calling poll().
        self._pending = collections.deque(maxlen=200)
        self._ser = None
        # All serial I/O happens on one worker thread (see _worker()). The rest
        # of the program only queues lines (_write) and reads lines (poll).
        self._tx = queue.Queue()
        self._last_kind = None   # "E" or "M": which stream the loop last fed
        self._last_tx = 0.0
        self._thread = None
        if port is None:
            self.port = None
            print("Serial: disabled (no --port) -- detection only")
            return
        if str(port).lower() == "auto":
            port = autodetect_port(baud)
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
        self._thread = threading.Thread(
            target=self._worker, name="esp-link", daemon=True)
        self._thread.start()

    def _worker(self):
        """The only code that touches the serial port.

        Loop: write whatever the program queued, read whatever the board sent,
        and keep the link alive. Doing this off the main thread means a stalled
        main loop cannot stall the link. Windows blocks the thread that owns a
        window while it is dragged or resized, and that is the vision loop, so
        without this no E/M frames would go out until the mouse is released and
        the firmware would drop the link after 300 ms and reset its PIDs.

        Keepalive: once the E/M stream has been quiet for KEEPALIVE_AFTER, send
        the harmless frame for whichever stream was active - `valid=0` (hold) or
        zero velocity - never the stale target, so the gimbal does not chase an
        old error while nobody looks. These are not written to the tx log.
        """
        ser = self._ser
        next_keepalive = 0.0
        while True:
            try:
                line = self._tx.get(timeout=0.01)
            except queue.Empty:
                line = ""
            if line is None:            # close(): everything queued was sent
                return
            try:
                if line:
                    ser.write(line.encode("ascii"))
                    continue            # more may be queued; reading can wait
                self._read(ser)
                now = time.time()
                if (self._last_kind is not None and now >= next_keepalive
                        and now - self._last_tx >= KEEPALIVE_AFTER):
                    ser.write(b"E 0.0000 0.0000 0\n" if self._last_kind == "E"
                              else b"M 0.000 0.000\n")
                    next_keepalive = now + 0.05
            except Exception as e:      # a USB hiccup must not kill the link
                print("Serial I/O failed:", e)
                time.sleep(0.05)

    def send(self, dx, dy, valid):
        """Send one frame. Returns True if it went out, False if rate-limited."""
        now = time.time()
        if now - self._last < self.min_interval:
            return False
        self._last = now

        if not (math.isfinite(dx) and math.isfinite(dy)):
            dx, dy, valid = 0.0, 0.0, False
        line = f"E {dx:.4f} {dy:.4f} {1 if valid else 0}\n"

        self._last_kind = "E"
        if self._write(line):
            self.sent += 1
        return True

    def manual(self, vpan_deg_s, vtilt_deg_s):
        """Send one `M <vpan> <vtilt>` frame -- direct velocity, deg/s, the
        MANUAL channel. Returns True if it went out, False if rate-limited.

        Rate-limited the same way send() rate-limits E frames, and for the
        same reason: MANUAL fails safe within config::TRACK_TIMEOUT_MS
        (300 ms) of the last M frame, exactly like AUTO does on E frames. A
        caller driving this from a key held down must keep calling it every
        loop iteration -- zero velocity included -- rather than only when the
        commanded rate changes; see manual_control.py.
        """
        now = time.time()
        if now - self._last_manual < self.min_interval:
            return False
        self._last_manual = now

        if not (math.isfinite(vpan_deg_s) and math.isfinite(vtilt_deg_s)):
            vpan_deg_s, vtilt_deg_s = 0.0, 0.0
        self._last_kind = "M"
        if self._write(f"M {vpan_deg_s:.3f} {vtilt_deg_s:.3f}\n"):
            self.sent += 1
        return True

    def manual_now(self, vpan_deg_s, vtilt_deg_s):
        """manual() ignoring the rate limit -- for an immediate stop."""
        self._last_manual = 0.0
        return self.manual(vpan_deg_s, vtilt_deg_s)

    def fire(self):
        """Pull the trigger: one laser flash. Never rate-limited or dropped.

        A one-shot frame of its own rather than a field of the E frame -- an E
        frame is a measurement that may be dropped or superseded at will, and
        nothing that can be dropped should be able to fire the laser.
        """
        self.fired += 1
        self._write("F\n")

    # --- tuning console --------------------------------------------------------
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

    def move_to(self, pan_deg, tilt_deg):
        """`P <pan> <tilt>` - drive both axes straight to an absolute angle,
        in degrees, clamped by the firmware to the current working zone
        (`Gimbal::moveTo()`). Unlike nudge(), this is not relative to wherever
        the gimbal currently is, and unlike E/M it bypasses the PID and the
        selected channel entirely -- a direct positioning primitive for bench
        use, e.g. center().
        """
        self.send_raw(f"P {pan_deg:g} {tilt_deg:g}\n")

    def center(self, timeout=0.4):
        """Move the gimbal to the centre of the *current* working zone.

        Reads zone.{pan,tilt}.{min,max} live (see read_zone()) rather than
        assuming the PC's own working-zone fields match what is actually
        applied on the board, then sends the midpoint with move_to(). Returns
        the (pan, tilt) centre sent. Raises RuntimeError if the zone could not
        be read within `timeout` (board unresponsive or not connected).
        """
        zone = self.read_zone(timeout)
        missing = [k for k in ("pan_min", "pan_max", "tilt_min", "tilt_max") if k not in zone]
        if missing:
            raise RuntimeError(f"no reply for {', '.join(missing)} "
                               "-- is the board connected?")
        pan = (zone["pan_min"] + zone["pan_max"]) / 2.0
        tilt = (zone["tilt_min"] + zone["tilt_max"]) / 2.0
        self.move_to(pan, tilt)
        return pan, tilt

    def telemetry(self, on):
        """`T <0|1>` - start/stop the plottable per-frame stream.

        It shares this UART with the frames we are sending, but the overlay
        renders it live (see overlay.draw_overlay), so detect_dots.py leaves
        it on by default rather than only while tuning.
        """
        self.telemetry_wanted = bool(on)
        self.send_raw(f"T {1 if on else 0}\n")

    def query(self):
        """`Q` - ask for the current gains; the firmware replies with `G ...`."""
        self.send_raw("Q\n")

    def send_ndjson(self, obj):
        """Seal `obj` with its CRC-8 and send it as one NDJSON line.

            {"t":"cfg.set","k":"input.channel","v":"AUTO","id":17}*4C

        docs/protocol.md §3.1/§3.2. `obj` must be JSON-serialisable and small
        enough to stay under the 256-byte line cap once sealed.
        """
        body = json.dumps(obj, separators=(",", ":"))
        self.send_raw(f"{body}*{crc8(body.encode('utf-8')):02X}\n")

    def cfg_set(self, key, value):
        """`cfg.set` - set one config key live (docs/protocol.md §3.3).

        Fire-and-forget from here: every cfg.set gets a matching `cfg.state`
        reply (see parse_cfg_state), accepted or rejected, which is how a
        typo'd key or an out-of-range value becomes visible rather than a
        silent no-op. Returns the `id` sent, so a caller can match it up.
        """
        self._cfg_id += 1
        self.send_ndjson({"t": "cfg.set", "k": key, "v": value, "id": self._cfg_id})
        return self._cfg_id

    def cfg_get(self, key):
        """`cfg.get` - read one config key (docs/protocol.md §3.3).

        Fire-and-forget from here too: the reply is a `cfg.state` line seen
        through poll(), matched by the returned id (see parse_cfg_state).
        """
        self._cfg_id += 1
        self.send_ndjson({"t": "cfg.get", "k": key, "id": self._cfg_id})
        return self._cfg_id

    def _read_keys(self, name_to_key, timeout):
        """cfg.get every key in `name_to_key` ({result_name: wire_key}), then
        block briefly draining poll() for up to `timeout` seconds matching
        `cfg.state` replies back to their request by id -- the same one-shot
        pattern _print_replies() uses below.

        Returns a dict with whichever names actually got a numeric reply in
        time; a missing name means no reply arrived (board unresponsive or
        not connected), which callers treat as a failure rather than guess.
        """
        wanted = {self.cfg_get(key): name for name, key in name_to_key.items()}
        out = {}
        deadline = time.time() + timeout
        while time.time() < deadline and len(out) < len(wanted):
            for line in self.poll():
                cfg = parse_cfg_state(line)
                if cfg is None or cfg.get("id") not in wanted:
                    continue
                try:
                    out[wanted[cfg["id"]]] = float(cfg.get("v"))
                except (TypeError, ValueError):
                    pass
            time.sleep(0.02)
        return out

    def read_zone_limits(self, timeout=0.4):
        """The gimbal's hard mechanical travel, read live from the firmware's
        read-only `zone.limit.{pan,tilt}.{min,max}` keys (docs/protocol.md
        §3.3) -- straight from Config.hpp's GIMBAL_PAN_MIN/MAX,
        GIMBAL_TILT_MIN/MAX rather than a copy of those numbers kept here.
        See _read_keys() for the blocking/missing-key behaviour.
        """
        return self._read_keys({
            "pan_min": "zone.limit.pan.min", "pan_max": "zone.limit.pan.max",
            "tilt_min": "zone.limit.tilt.min", "tilt_max": "zone.limit.tilt.max",
        }, timeout)

    def read_zone(self, timeout=0.4):
        """The *current* working zone, read live from `zone.{pan,tilt}.{min,max}`
        -- the bounds actually in effect right now, as opposed to
        read_zone_limits()'s fixed mechanical ceiling. See _read_keys() for
        the blocking/missing-key behaviour.
        """
        return self._read_keys({
            "pan_min": "zone.pan.min", "pan_max": "zone.pan.max",
            "tilt_min": "zone.tilt.min", "tilt_max": "zone.tilt.max",
        }, timeout)

    def set_channel(self, channel):
        """cfg.set `input.channel` - NONE / AUTO / MANUAL.

        This is what makes this script's own `E` frames (the AUTO channel)
        actually move the gimbal: the firmware boots with input.channel =
        NONE, and every frame on a non-selected channel is parsed, counted as
        drop_inact, and thrown away before it reaches the controller (§2.1).
        MANUAL is the keyboard-driven `M <vpan> <vtilt>` channel (see
        manual_control.py / the `manual()` method), not this script's own.
        """
        c = str(channel).strip().upper()
        if c not in ("NONE", "AUTO", "MANUAL"):
            raise ValueError(f"channel must be NONE/AUTO/MANUAL (got {channel!r})")
        self.channel_requested = c
        return self.cfg_set("input.channel", c)

    def press_control(self):
        """cfg.set `control.press` - the remote equivalent of pressing the
        board's physical CONTROL button.

        Same toggle the button gives: arms from DISARMED/PARKED, disarms from
        ARMED/LINK_LOST, acknowledges a latched FAULT, no-ops during
        BOOT/SELFTEST/ZONE_TOUR. AUTO-channel `E` frames only move the gimbal
        once `st:` (the telemetry readout) reads ARMED -- selecting the
        channel alone is not enough.

        This is the one command that is not gated by input.channel: it works
        regardless of which channel is selected, same as the physical button.
        Watch `st:` after sending it to see which of the above happened.
        """
        return self.cfg_set("control.press", True)

    def set_zone(self, pan_min, pan_max, tilt_min, tilt_max):
        """cfg.set the four `zone.{pan,tilt}.{min,max}` keys - the working
        area the gimbal is allowed to move in, narrower than (and clamped to)
        the mechanical limits.

        There is no batched cfg.set on the wire, so this is four separate
        lines, each with its own id and its own `cfg.state` ack - a bad value
        on one axis does not block the others. Returns the four ids, in
        (pan_min, pan_max, tilt_min, tilt_max) order, so a caller can match
        them up. Takes effect live (no reboot needed); use zone_tour() to see
        the new bounds traced out.
        """
        return (self.cfg_set("zone.pan.min", pan_min),
                self.cfg_set("zone.pan.max", pan_max),
                self.cfg_set("zone.tilt.min", tilt_min),
                self.cfg_set("zone.tilt.max", tilt_max))

    def zone_tour(self):
        """cfg.set `control.zone_tour` - restart the ZONE_TOUR bench sweep of
        the *current* working zone, laser lit, without a reboot.

        Same action-key trade as press_control(): no-ops outside
        DISARMED/PARKED (a tour must not hijack the gimbal from an active
        operator), always acks ok:true regardless, so watch `st:` in
        telemetry for ZONE_TOUR to confirm it actually started.
        """
        return self.cfg_set("control.zone_tour", True)

    def set_boot_tour(self, enabled):
        """cfg.set `boot.tour` - whether the board runs ZONE_TOUR after
        SELFTEST at every reset (off: SELFTEST goes straight to DISARMED).

        Stored config, not an action: saved in NVS, applies at the next
        reset. The `cfg.state` reply confirms the value.
        """
        return self.cfg_set("boot.tour", bool(enabled))

    def send_raw(self, line):
        self._write(line)

    def _write(self, line):
        """Queue one line for the worker. True if a port will send it.

        Log the attempt regardless of whether it actually reaches the wire
        -- see tx_log.py. This is the one call site every sender above
        goes through, so it is also the one place logging needs to happen.
        """
        self._tx_log.record(line)
        if self._ser is None:
            return False
        if line[:1] in ("E", "M"):
            self._last_tx = time.time()
        self._tx.put(line)
        return True

    def poll(self):
        """Whatever the firmware has said since the last call, as text lines.

        The worker reads the port continuously; this only hands over what it
        has collected. Only protocol lines are expected on this UART (console
        logs are UART0 / USB-Serial-JTAG, not this port -- see the module
        docstring). Call it every frame or the oldest lines fall off.
        """
        lines = []
        while self._pending:
            try:
                lines.append(self._pending.popleft())
            except IndexError:
                break
        return lines

    def _read(self, ser):
        """Worker only: move whatever the port has into `_pending`."""
        waiting = ser.in_waiting
        if not waiting:
            return
        self._rx += ser.read(waiting)
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

    def close(self, park=True):
        if self._ser is not None:
            if park:
                # Zero both channels: whichever one is actually selected
                # is what stops the gimbal, and there is no way to read
                # input.channel back from here to send only the right one.
                self.send_now(0.0, 0.0, False)   # AUTO
                self.manual_now(0.0, 0.0)        # MANUAL
            self._tx.put(None)                   # worker exits after sending these
            self._thread.join(timeout=1.0)
            try:
                self._ser.close()
            except Exception:
                pass
            self._ser = None
        self._tx_log.close()

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
    tlm = parse_tlm(line)
    if tlm is not None:
        return (f"tlm: ex:{tlm['ex']:+.3f} ey:{tlm['ey']:+.3f} "
                f"v:{tlm['vp']:+.1f}/{tlm['vt']:+.1f} deg/s "
                f"pan:{tlm['pan']:.1f} tilt:{tlm['tilt']:.1f} "
                f"[{tlm['st']}/{tlm['ch']}]")
    cfg = parse_cfg_state(line)
    if cfg is not None:
        status = "OK" if cfg.get("ok") else f"REJECTED ({cfg.get('err')})"
        return f"cfg: {cfg.get('k')}={cfg.get('v')!r} {status} [id {cfg.get('id')}]"
    tel = parse_telemetry(line)   # retired ASCII format, older firmware only
    if tel is not None:
        return (f"ex:{tel['ex']:+.3f} ey:{tel.get('ey', 0.0):+.3f} "
                f"pan:{tel.get('pan', 0.0):.1f} tilt:{tel.get('tilt', 0.0):.1f} "
                f"[{tel['st']}]" + (" ARRIVED" if tel["arr"] else ""))
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
          "  E 0.2 0 1         one error frame (AUTO channel)\n"
          "  M 20 0            one velocity frame, deg/s (MANUAL channel)\n"
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
    ap.add_argument("--tx-log", nargs="?", const="tx_log.txt", default=None,
                    metavar="PATH",
                    help="append every line sent to PATH (bare --tx-log = "
                         "tx_log.txt), timestamped -- see tx_log.py. "
                         "Everything prints to the console regardless except "
                         "streamed E frames, which need --echo")

    # --- tuning console ---
    ap.add_argument("--query", action="store_true",
                    help="Q: print the current gains and arm state, then exit")
    ap.add_argument("--gains", nargs=4, metavar=("AXIS", "KP", "KI", "KD"),
                    help="K: set gains live, AXIS = p | t | b, e.g. --gains b 40 4 0")
    ap.add_argument("--nudge", nargs=2, type=float, metavar=("DPAN", "DTILT"),
                    help="N: displace the gimbal open loop, in degrees - a "
                         "repeatable disturbance for the loop to reject")
    ap.add_argument("--manual", nargs=2, type=float, metavar=("VPAN", "VTILT"),
                    help="M: one direct velocity command, deg/s - only moves "
                         "the gimbal once input.channel=MANUAL and ARMED "
                         "(--channel MANUAL --control); see manual_control.py "
                         "for the keyboard-driven version")
    ap.add_argument("--telemetry", type=int, choices=[0, 1], metavar="0|1",
                    help="T: start/stop the plottable per-frame stream")
    ap.add_argument("--channel", choices=["NONE", "AUTO", "MANUAL"],
                    help="cfg.set input.channel: AUTO is what makes this "
                         "script's own E frames (or detect_dots.py's) take "
                         "effect -- the firmware boots with it at NONE")
    ap.add_argument("--control", action="store_true",
                    help="cfg.set control.press: remote CONTROL-button press "
                         "-- arm/disarm toggle, or fault.ack if latched. "
                         "Selecting AUTO alone does not move the gimbal; it "
                         "also has to be ARMED (see st: in the telemetry)")
    ap.add_argument("--zone", nargs=4, type=float,
                    metavar=("PAN_MIN", "PAN_MAX", "TILT_MIN", "TILT_MAX"),
                    help="cfg.set zone.{pan,tilt}.{min,max}: the working area, "
                         "in degrees -- narrower than (and clamped to) the "
                         "mechanical limits. Takes effect live")
    ap.add_argument("--zone-tour", action="store_true",
                    help="cfg.set control.zone_tour: restart the ZONE_TOUR "
                         "bench sweep of the current working zone, laser lit, "
                         "without a reboot. No-ops outside DISARMED/PARKED "
                         "(see st: in the telemetry)")
    ap.add_argument("--boot-tour", choices=("on", "off"),
                    help="cfg.set boot.tour: run the ZONE_TOUR sweep after "
                         "SELFTEST at every reset. Saved in NVS; applies at "
                         "the next reset")
    ap.add_argument("--zone-limits", action="store_true",
                    help="cfg.get zone.limit.{pan,tilt}.{min,max}: print the "
                         "gimbal's hard mechanical travel, read live from the "
                         "firmware")
    ap.add_argument("--move-to", nargs=2, type=float, metavar=("PAN", "TILT"),
                    help="P: drive both axes straight to an absolute angle, "
                         "in degrees, clamped to the current working zone -- "
                         "bypasses the PID and the selected channel entirely")
    ap.add_argument("--center", action="store_true",
                    help="read the current working zone live and move to its "
                         "midpoint (P PAN TILT); see --move-to for an "
                         "arbitrary position")
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
    if (args.query or args.gains or args.nudge or args.manual
            or args.telemetry is not None or args.channel or args.control
            or args.zone or args.zone_tour or args.zone_limits
            or args.boot_tour or args.move_to or args.center):
        link = ErrorLink(args.port, args.baud, echo=False, tx_log_path=args.tx_log)
        try:
            if args.channel:
                link.set_channel(args.channel)
            if args.control:
                link.press_control()
            if args.zone:
                link.set_zone(*args.zone)
            if args.zone_tour:
                link.zone_tour()
            if args.boot_tour:
                link.set_boot_tour(args.boot_tour == "on")
            if args.zone_limits:
                limits = link.read_zone_limits()
                print("  zone limits:", limits or "(no reply)")
            if args.move_to:
                link.move_to(*args.move_to)
            if args.center:
                try:
                    pan, tilt = link.center()
                    print(f"  centered at pan={pan:g} tilt={tilt:g}")
                except RuntimeError as e:
                    print(f"  --center failed: {e}")
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
            if args.manual:
                link.manual(*args.manual)
            if args.query:
                link.query()
            _print_replies(link, 0.4)
        finally:
            link.close(park=False)       # sent no E frames; nothing to park
        return

    if args.console:
        _console(ErrorLink(args.port, args.baud, echo=True, tx_log_path=args.tx_log))
        return

    if args.monitor:
        # Pure listener - the gimbal never moves, so this is the safe way to
        # check the wiring and see the firmware's banner and telemetry.
        link = ErrorLink(args.port, args.baud, echo=True, tx_log_path=args.tx_log)
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

    link = ErrorLink(args.port, args.baud, max_rate=args.rate * 2, echo=args.echo,
                     tx_log_path=args.tx_log)
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
