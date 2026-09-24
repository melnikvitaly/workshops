"""OAK camera -> red dot + black target dot -> error vector -> ESP32 over COM.

The PC end of the closed loop implemented by src/inputs/ErrorVectorInput.hpp.
Each frame:

    frame ─┬─ red   dot (redness + size)     ─> where the laser points now
           └─ black dots (adaptive + shape)  ─> where it should point
                                    │
                          error = target - laser, normalised
                                    │
                    "E <dx> <dy> <valid>\\n"  -> COM port -> ESP32 UART

Every frame is also rendered with its detections drawn on it (both dots, which
black dot was chosen, and the error vector as an arrow), and the window carries
a FIRE button that sends "F\\n" -- the PC-side trigger, equivalent to clicking
the button on the board.

Detection runs entirely on the host (see dots.py); the OAK is used as a camera.
The camera plumbing is DepthAI v3 and mirrors the on-camera NN pipeline used on
other projects, so the same pipeline shape works for both.

This file is the frame sources, the loop, and the command line. The display
lives elsewhere: app_window.py is the single Tk window (controls on the left,
camera view in the middle, tuning on the right), overlay.py draws on the frame and words the status text,
controls.py is the gains/protocol panel, tuning.py is the threshold sliders,
simulated_target.py turns clicks into a stand-in target dot.

Usage:
    py -3 tracker.py --port                          # live OAK -> ESP32 (auto-found)
    py -3 tracker.py --port COM5                     # ... or name the port
    py -3 tracker.py                                 # live OAK, no serial
    py -3 tracker.py --source shot.jpg --debug       # tune on one image
    py -3 tracker.py --source dataset/ --debug       # step through a folder
    py -3 tracker.py --source 0                      # any webcam, no OAK

Keys (with the view focused, not a text box): q = quit, f = fire, d = toggle
the mask view and the labelled rejections, p = print the current thresholds as
a command line, m = toggle keyboard MANUAL drive (see manual_control.py),
arrows = move the simulated target, or drive the gimbal while 'm' is engaged,
SPACE/n = next image (folder mode).
Mouse: left-click places the simulated target, right-click clears it.
"""

import argparse
import glob
import math
import os
import time

import cv2

from app_window import AppWindow
from dots import error_vector, find_black_dots, find_red_dot, pick_target
from manual_control import ManualControl
from recenter import LaserLostRecenter
from overlay import _ROTATE, draw_overlay, render_masks, status_lines
from serial_link import ErrorLink, list_ports, parse_tlm, parse_tlm_sys
from simulated_target import SimulatedTargetManager
from speed import SpeedSettings
from tuning import Thresholds

_IMG_EXT = (".jpg", ".jpeg", ".png", ".bmp")
_VID_EXT = (".mp4", ".avi", ".mov", ".mkv")


# --- frame sources ---------------------------------------------------------

def _set_focus(dai, control_queue, focus):
    """Autofocus off, lens fixed at `focus` (0..255). Applied at once."""
    ctrl = dai.CameraControl()
    ctrl.setAutoFocusMode(dai.CameraControl.AutoFocusMode.OFF)
    ctrl.setManualFocus(int(focus))
    control_queue.send(ctrl)


def _start_pipeline(dai, resolution, fps, queue_size, focus):
    """(running pipeline, frame queue, control queue). RuntimeError if the sensor refuses."""
    pipeline = dai.Pipeline()
    try:
        cam = pipeline.create(dai.node.Camera).build()
        camera_out = cam.requestOutput(resolution, type=dai.ImgFrame.Type.BGR888i,
                                       fps=fps)
        video_queue = camera_out.createOutputQueue(maxSize=queue_size,
                                                   blocking=False)
        control_queue = cam.inputControl.createInputQueue()
        pipeline.start()
        _set_focus(dai, control_queue, focus)
    except RuntimeError:
        try:
            pipeline.stop()
        except RuntimeError:
            pass
        raise
    return pipeline, video_queue, control_queue


def camera_frames(resolution, speed):
    """Yield BGR frames from the OAK camera until the pipeline stops.

    `speed` (see speed.py) holds the live settings, read again every frame:

      fps        -- the sensor frame rate. A change rebuilds the pipeline.
      queue_size -- how many frames may wait for us. With a non-blocking queue
                    get() returns the OLDEST waiting frame, so a deep queue
                    means we detect on old images whenever we are slower than
                    the camera. 1 always gives the newest (lowest latency).
                    Changed on the running queue when the camera allows it,
                    otherwise by rebuilding the pipeline.
      focus      -- fixed lens position 0..255. Autofocus is always off: it
                    hunts and blurs the dot. Changed live, no restart.
    """
    import depthai as dai
    good_fps = None   # last fps that actually started
    while True:
        fps, queue_size, focus = speed.fps, speed.queue_size, speed.focus
        try:
            pipeline, video_queue, control_queue = _start_pipeline(
                dai, resolution, fps, queue_size, focus)
        except RuntimeError as exc:
            # The sensor only offers some size/fps pairs (12 MP tops out at
            # 30 fps). Refusing one must not end the run: go back to the last
            # fps that worked, or at startup step down to the next lower one.
            print(f"OAK refused {resolution[0]}x{resolution[1]} @ {fps:g} fps: "
                  f"{str(exc).splitlines()[0]}")
            fallback = good_fps if good_fps is not None else next(
                (f for f in (30.0, 15.0) if f < fps), None)
            if fallback is None or fallback == fps:
                raise
            speed.refused(fallback, queue_size,
                          f"{resolution[0]}x{resolution[1]} can't do {fps:g} fps")
            continue
        good_fps = fps
        print(f"OAK pipeline started: {fps:g} fps, queue {queue_size}.")
        restart = False
        with pipeline:
            while pipeline.isRunning():
                if speed.fps != fps:
                    restart = True
                    break
                if speed.focus != focus:
                    _set_focus(dai, control_queue, speed.focus)
                    focus = speed.focus
                if speed.queue_size != queue_size:
                    try:
                        video_queue.setMaxSize(speed.queue_size)
                        queue_size = speed.queue_size
                    except AttributeError:
                        restart = True
                        break
                yield video_queue.get().getCvFrame()
        if not restart:
            return


def capture_frames(source):
    """Yield BGR frames from a webcam index or a video file."""
    cap = cv2.VideoCapture(int(source) if str(source).isdigit() else source)
    if not cap.isOpened():
        print(f"Could not open {source}")
        return
    try:
        while True:
            ok, frame = cap.read()
            if not ok:
                return
            yield frame
    finally:
        cap.release()


def file_frames(source):
    """Yield frames from an image file or a folder of images.

    For a folder we advance one image per keypress in the main loop, so this
    generator just hands out each image once. A single image is held forever, so
    the serial stream keeps running while you tune thresholds against it.
    """
    if os.path.isdir(source):
        paths = sorted(p for p in glob.glob(os.path.join(source, "*"))
                       if p.lower().endswith(_IMG_EXT))
        if not paths:
            print(f"No images in {source}")
            return
        print(f"{len(paths)} images. SPACE/n = next, q = quit.")
        for p in paths:
            frame = cv2.imread(p)
            if frame is not None:
                print(f"-> {os.path.basename(p)}")
                yield frame
    else:
        frame = cv2.imread(source)
        if frame is None:
            print(f"Could not read {source}")
            return
        while True:
            yield frame.copy()


def open_source(args, speed):
    """(frame generator, folder_mode) for whatever --source asks for."""
    src = args.source
    if src == "cam":
        return camera_frames((args.width, args.height), speed), False
    if str(src).isdigit() or src.lower().endswith(_VID_EXT):
        return capture_frames(src), False
    return file_frames(src), os.path.isdir(src)


# --- main loop -------------------------------------------------------------

def _recenter_step(link, recenter, telemetry, telemetry_at):
    """Send the next waypoint toward the zone centre (a `P` frame).

    The centre is read from the board once per lost event and the start
    position comes from the last `tlm` sample. Without a fresh sample the
    position is unknown, so this falls back to one jump to the centre
    (link.center()) that cannot be cancelled. Raises RuntimeError if the board
    does not answer.
    """
    if not recenter.started:
        if telemetry is None or time.monotonic() - telemetry_at > 1.0:
            centre = link.center()
            recenter.begin(centre, centre)      # already there: no more steps
            return
        zone = link.read_zone()
        if any(k not in zone for k in ("pan_min", "pan_max", "tilt_min", "tilt_max")):
            raise RuntimeError("zone not readable - is the board connected?")
        recenter.begin((telemetry["pan"], telemetry["tilt"]),
                       ((zone["pan_min"] + zone["pan_max"]) / 2.0,
                        (zone["tilt_min"] + zone["tilt_max"]) / 2.0))
    wp = recenter.step(time.monotonic())
    if wp is not None:
        link.move_to(*wp)


def run(args):
    link = ErrorLink(args.port, args.baud, max_rate=args.rate, echo=args.echo,
                     tx_log_path=args.tx_log)
    speed = SpeedSettings(args, link)
    frames, folder_mode = open_source(args, speed)
    fps, last_t = 0.0, time.time()

    telemetry = None
    telemetry_at = 0.0
    sys_tlm = None      # latest tlm.sys (1 Hz): PID rate, heap, ...
    sys_tlm_at = 0.0
    telemetry_requested_at = 0.0
    # The simulated target manager turns clicks on the view into a stand-in
    # black dot, mapping display coordinates back through any --rotate.
    sim = SimulatedTargetManager(rotate=args.rotate)
    # The flags set the starting point; from here the sliders own these values.
    th = Thresholds(args)
    win = None
    if not args.headless:
        win = AppWindow(link, th, speed, debug=args.debug)
        win.on_click = sim.place_display
        win.on_clear = sim.clear
    manual = ManualControl(link, speed_deg_s=args.manual_speed,
                           active=win.keys_ready if win else None)
    drive_shown = False
    recenter = LaserLostRecenter(args.recenter_ms, args.recenter_speed)
    recentering = False

    print("Press 'q' to quit, 'f' or the FIRE button to flash the laser, "
          "'m' to toggle keyboard MANUAL drive (arrow keys)."
          + ("  SPACE/n = next image." if folder_mode else ""))
    try:
        for frame in frames:
            # The sliders write straight into `th` from Tk callbacks, which only
            # run inside win.pump() below - never mid-frame - so this whole
            # frame is detected with one consistent set of thresholds.
            debug = win.debug.get() if win is not None else args.debug
            red, red_mask = find_red_dot(frame, *th.red_args())
            targets, black_mask, rejects = find_black_dots(frame, *th.black_args())
            target = pick_target(targets, frame.shape, args.target, red)
            # If detection found no black target, fall back to a simulated one
            # created by a user click.
            sim.update_state(targets, frame.shape)
            if target is None and sim.get_simulated() is not None:
                target = sim.get_simulated()

            dx, dy, valid = error_vector(red, target, frame.shape)
            if win is not None:
                win.record_error(dx, dy)
            # Laser outside the camera view: nothing to correct, so drift toward
            # the zone centre until the red dot is seen again. Runs after the
            # detection above and stops with it -- see recenter.py.
            recenter_wanted = (win.controls.recenter_on.get() if win is not None
                               else True)
            if (recenter_wanted and not manual.engaged and link.port is not None
                    and recenter.update(red is not None, time.monotonic())):
                recentering = True
                try:
                    _recenter_step(link, recenter, telemetry, telemetry_at)
                except RuntimeError as exc:
                    print(f"recenter: {exc}")
            else:
                recentering = False
                if not recenter_wanted or manual.engaged:
                    recenter.reset()
            # On by default, and kept that way: re-ask for telemetry (at most
            # once a second) whenever no tlm sample has landed in the last 2s
            # and nobody has asked for it to be off. A single T 1 right after
            # open is not enough -- opening the port can itself reboot the
            # board (same edge _handshake_ok works around for Q), and unlike Q
            # there is no reply that confirms a T landed, so the first one is
            # easily lost to a UART that is not listening yet. Keying off
            # staleness rather than "never received" also recovers telemetry
            # after a mid-session board reset, not just the initial boot race;
            # link.telemetry_wanted is what stops this from fighting an
            # operator who unticked the checkbox (or sent T 0 some other way).
            if (not args.headless and link.port is not None and link.telemetry_wanted
                    and time.monotonic() - telemetry_at > 2.0
                    and time.monotonic() - telemetry_requested_at > 1.0):
                link.telemetry(True)
                telemetry_requested_at = time.monotonic()
            # Send every frame, valid or not: the firmware treats silence as a
            # dead link (300 ms) and resets its PIDs, while valid=0 only holds.
            on_wire = link.send(dx, dy, valid)
            # Drain every UART line so console traffic cannot fill the OS buffer.
            # Only explicit uplink protocol messages affect the UI.
            for esp_line in link.poll():
                sample = parse_tlm(esp_line)
                if sample is not None:
                    telemetry = sample
                    telemetry_at = time.monotonic()
                    manual.note_channel(sample.get("ch"))
                    if win is not None:
                        win.controls.note_state(sample.get("st"))
                    continue
                sys_sample = parse_tlm_sys(esp_line)
                if sys_sample is not None:
                    sys_tlm = sys_sample
                    sys_tlm_at = time.monotonic()

            # Display-only: how close counts as "on target" for the border
            # colour. Independent of the firmware's own (much tighter) deadzone,
            # which is what actually decides when it reports arrival.
            on_target = valid and math.hypot(dx, dy) <= args.ready_error

            now = time.time()
            fps = 0.9 * fps + 0.1 / max(now - last_t, 1e-6)
            last_t = now
            # Never hidden once received; its growing age is what tells the
            # operator the link died, rather than the readout vanishing.
            telemetry_age = (time.monotonic() - telemetry_at) if telemetry is not None else None

            if win is not None:
                shown_rejects = rejects if debug else ()
                win.set_status(status_lines(red, targets, target, dx, dy, valid,
                                            fps, frame.shape, link, telemetry,
                                            telemetry_age, shown_rejects,
                                            recentering, sys_tlm,
                                            (time.monotonic() - sys_tlm_at)
                                            if sys_tlm is not None else None))
                view = draw_overlay(frame, red, targets, target, valid,
                                    shown_rejects)
                if args.rotate:
                    view = cv2.rotate(view, _ROTATE[args.rotate])
                win.set_on_target(bool(on_target))
                win.show_frame(view)
                if debug:
                    win.show_masks(render_masks(red_mask, black_mask, args.rotate))
                key = win.wait_key(folder_mode)
                if manual.toggle_pressed(key):
                    on = manual.toggle()
                    print("keyboard MANUAL drive "
                          + ("ON -- arrow keys pan/tilt" if on else "off"))
                if manual.engaged != drive_shown:
                    drive_shown = manual.engaged
                    win.set_drive(drive_shown)
                try:
                    # manual.handle_key() only consumes arrows while keyboard
                    # drive is engaged, so sim's arrow-key target nudge still
                    # works normally the rest of the time.
                    if manual.handle_key(key):
                        continue
                    if sim.handle_key(key):
                        continue
                except Exception:
                    pass
                if key == "q" or not win.alive:
                    break
                if key == "f":
                    win.flash_fire()
                    link.fire()
                elif win.take_fire():
                    win.flash_fire()
                    link.fire()
                # Simulated targets persist across keypresses; right-click in
                # the view clears them.
                if key == "d":
                    win.toggle_debug()
                if key == "p":
                    # Sliders are lost on exit; this is how a session's tuning
                    # becomes the next run's command line.
                    print(th.flags())
                # MANUAL fails safe the same 300 ms way the Auto-family channels do, so this
                # must run every iteration, key or not, same as link.send()
                # above for the E frames.
                manual.tick()
            else:
                if args.verbose and on_wire:
                    print(f"E {dx:+.4f} {dy:+.4f} {1 if valid else 0}")
                # Nothing paces a headless still-image run; don't spin a core.
                time.sleep(link.min_interval / 4.0)
    except KeyboardInterrupt:
        pass
    finally:
        link.close()
        if win is not None:
            win.close()
    print(f"{link.sent} frames sent.")


def main():
    ap = argparse.ArgumentParser(
        description="Red dot + black target dot -> error vector -> ESP32 UART")
    ap.add_argument("--source", default="cam",
                    help="'cam' (OAK), an image, a folder, a video file, or a "
                         "webcam index")
    ap.add_argument("--width", type=int, default=1280, help="OAK frame width")
    ap.add_argument("--height", type=int, default=720, help="OAK frame height")
    ap.add_argument("--fps", type=float, default=60.0,
                    help="OAK sensor frame rate. The firmware uses one frame "
                         "per 20 ms control step, so more than 50 is wasted")
    ap.add_argument("--queue-size", type=int, default=1,
                    help="OAK frames buffered for us; 1 = always the newest "
                         "frame (lowest latency), higher = older frames")
    ap.add_argument("--focus", type=int, default=130,
                    help="OAK lens position 0..255. Autofocus is off, the lens "
                         "stays here. Tune for the wall distance (the Speed box "
                         "changes it live)")
    ap.add_argument("--rotate", type=int, default=0, choices=[0, 90, 180, 270],
                    help="rotate the DISPLAY N degrees CCW (detection unaffected)")

    # --- serial link to the ESP32 ---
    ap.add_argument("--port", default=None, nargs="?", const="auto",
                    help="COM port of the ESP32-S3, e.g. COM5. Bare --port (or "
                         "--port auto) finds the board by USB VID/PID. Omitted "
                         "entirely = detect only, send nothing")
    ap.add_argument("--list-ports", action="store_true",
                    help="list the serial ports, marking the likely board, and exit")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--rate", type=float, default=50.0,
                    help="max frames/second put on the wire. The firmware "
                         "control step is 50 Hz, so 50 is the useful maximum")
    ap.add_argument("--echo", action="store_true",
                    help="print every received ESP32 line, and every sent E "
                         "frame too (everything else already prints "
                         "regardless); console text is never shown in the "
                         "window")
    ap.add_argument("--tx-log", nargs="?", const="tx_log.txt", default=None,
                    metavar="PATH",
                    help="append every command line sent to PATH, timestamped "
                         "(bare --tx-log = tx_log.txt) -- see tx_log.py")
    ap.add_argument("--manual-speed", type=float, default=40.0,
                    help="deg/s commanded by each arrow key while keyboard "
                         "MANUAL drive ('m') is engaged")

    ap.add_argument("--ready-error", type=float, default=0.02,
                    help="|error| at or below which the FIRE border turns red "
                         "(on target). Display only - the firmware decides "
                         "arrival on its own, tighter deadzone")

    # --- which black dot is the target ---
    ap.add_argument("--recenter-ms", type=float, default=1500.0,
                    help="red dot missing this long -> move the gimbal toward "
                         "the zone centre until the dot is seen again. "
                         "0 = off. The window also has a checkbox for it")
    ap.add_argument("--recenter-speed", type=float, default=30.0,
                    help="deg/s of that move toward the centre")
    ap.add_argument("--target", default="center",
                    choices=["center", "largest", "nearest"],
                    help="pick the black dot nearest the frame centre (default), "
                         "the largest one, or the one nearest the red dot")

    # --- red dot thresholds (areas are for a 640x480 reference frame) ---
    ap.add_argument("--red-area-min", type=int, default=3)
    ap.add_argument("--red-area-max", type=int, default=2000)
    ap.add_argument("--red-circ", type=float, default=0.35,
                    help="minimum circularity of the red blob")
    ap.add_argument("--red-min-redness", type=int, default=22,
                    help="absolute floor on R-max(G,B); below this the frame is "
                         "declared dot-free (raise if noise is detected as a dot)")
    ap.add_argument("--red-rel", type=float, default=0.5,
                    help="threshold as a fraction of the frame's peak redness "
                         "(lower = bigger, more forgiving blob)")

    # --- black dot: is it round? (measured defaults, see dots._shape_reject) ---
    ap.add_argument("--black-area-min", type=int, default=40)
    ap.add_argument("--black-area-max", type=int, default=20000)
    ap.add_argument("--black-circ", type=float, default=0.80,
                    help="minimum circularity: fraction of the smallest "
                         "enclosing circle the blob fills. A disc measures "
                         "0.82-0.98, a square 0.72, a fat ellipse 0.77")
    ap.add_argument("--black-radial", type=float, default=0.10,
                    help="max spread of the centre-to-edge distance (/mean). "
                         "The direct 'is every edge point equidistant' test; "
                         "lower it to ~0.05 to also refuse polygons")
    ap.add_argument("--black-aspect", type=float, default=1.25,
                    help="max long/short side of the blob's minimum-area "
                         "rectangle - rejects ellipses and rounded bars")
    ap.add_argument("--black-solidity", type=float, default=0.88,
                    help="min area / convex-hull area - rejects dents and "
                         "notches, e.g. two dots touching")
    ap.add_argument("--black-compact", type=float, default=0.50,
                    help="min 4*pi*area / perimeter^2 - rejects frayed or "
                         "knobbly outlines. Lower it if a rough print is "
                         "being dropped")
    ap.add_argument("--black-hole", type=float, default=0.15,
                    help="max enclosed background / blob area - rejects rings "
                         "and O-shapes, which every other test scores as "
                         "perfect circles")
    ap.add_argument("--black-edge-margin", type=int, default=2,
                    help="drop blobs within this many px of the frame border "
                         "(cut-off shapes measure as something else); -1 keeps "
                         "them")

    # --- black dot: is it ink? ---
    ap.add_argument("--black-darkness", type=float, default=0.8,
                    help="blob must be at most this fraction as bright as the "
                         "paper ringing it (lower = stricter)")
    ap.add_argument("--black-sat-margin", type=int, default=70,
                    help="how much more saturated than the surrounding paper a "
                         "blob may be before it counts as coloured, not ink")
    ap.add_argument("--black-block", type=int, default=51,
                    help="adaptive-threshold window in px (odd); roughly 3x the "
                         "dot diameter")
    ap.add_argument("--black-offset", type=int, default=12,
                    help="how much darker than its surroundings ink must be")

    ap.add_argument("--debug", action="store_true",
                    help="show the red / black binary masks, and box every "
                         "rejected blob on the frame with the measurement that "
                         "failed (also 'd' at runtime)")
    ap.add_argument("--headless", action="store_true",
                    help="no windows -- run the link without a display")
    ap.add_argument("--verbose", action="store_true",
                    help="print every frame sent (headless mode)")
    args = ap.parse_args()
    if args.list_ports:
        list_ports()
        return
    run(args)


if __name__ == "__main__":
    main()
