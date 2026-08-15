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
The camera plumbing is DepthAI v3 and mirrors ../../../final_project/camera-host
/detect.py, so the same pipeline shape works for both projects.

This file is the frame sources, the loop, and the command line. The display
lives elsewhere: overlay.py draws on the frame, fire_button.py is the FIRE
widget, controls.py is the gains/protocol window (Tk), tuning.py is the
threshold sliders, simulated_target.py turns clicks into a stand-in target dot.

Usage:
    py -3 detect_dots.py --port                          # live OAK -> ESP32 (auto-found)
    py -3 detect_dots.py --port COM5                     # ... or name the port
    py -3 detect_dots.py                                 # live OAK, no serial
    py -3 detect_dots.py --source shot.jpg --debug       # tune on one image
    py -3 detect_dots.py --source dataset/ --debug       # step through a folder
    py -3 detect_dots.py --source 0                      # any webcam, no OAK

Keys: q = quit, f = fire, d = toggle the mask windows, the labelled rejections
and the threshold sliders, p = print the current thresholds as a command line,
arrows = move the simulated target, SPACE/n = next image (folder mode).
Mouse: left-click places the simulated target, right-click clears it.
"""

import argparse
import glob
import math
import os
import time

import cv2

from controls import Controls
from dots import error_vector, find_black_dots, find_red_dot, pick_target
from fire_button import FireButton
from overlay import _ROTATE, _WIN, draw_overlay, hide_masks, show_masks
from serial_link import ErrorLink, list_ports, parse_telemetry
from simulated_target import SimulatedTargetManager
from tuning import Thresholds

_IMG_EXT = (".jpg", ".jpeg", ".png", ".bmp")
_VID_EXT = (".mp4", ".avi", ".mov", ".mkv")
_TELEMETRY_STALE_S = 2.0


# --- frame sources ---------------------------------------------------------

def camera_frames(resolution):
    """Yield BGR frames from the OAK camera until the pipeline stops."""
    import depthai as dai
    pipeline = dai.Pipeline()
    cam = pipeline.create(dai.node.Camera).build()
    camera_out = cam.requestOutput(resolution, type=dai.ImgFrame.Type.BGR888i)
    video_queue = camera_out.createOutputQueue(maxSize=4, blocking=False)
    pipeline.start()
    print("OAK pipeline started.")
    with pipeline:
        while pipeline.isRunning():
            yield video_queue.get().getCvFrame()


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


def open_source(args):
    """(frame generator, folder_mode) for whatever --source asks for."""
    src = args.source
    if src == "cam":
        return camera_frames((args.width, args.height)), False
    if str(src).isdigit() or src.lower().endswith(_VID_EXT):
        return capture_frames(src), False
    return file_frames(src), os.path.isdir(src)


# --- main loop -------------------------------------------------------------

def _wait_key(block, controls):
    """One key from the view window, keeping the Tk panel alive while waiting.

    HighGUI pumps its own windows from inside waitKey, but nobody else's, so a
    blocking `waitKeyEx(0)` -- what folder mode wants, one image per press --
    would freeze the controls window until a key arrived. Block in short slices
    and service Tk between them instead.
    """
    while True:
        if controls is not None:
            controls.pump()
        raw = cv2.waitKeyEx(15 if block else 1)
        if raw != -1 or not block:
            return raw


def run(args):
    frames, folder_mode = open_source(args)
    link = ErrorLink(args.port, args.baud, max_rate=args.rate, echo=args.echo)
    debug = args.debug
    fps, last_t = 0.0, time.time()

    fire = FireButton()
    telemetry = None
    telemetry_at = 0.0
    # The simulated target manager owns the view window's mouse: it lets the
    # FIRE button inspect each click first, then treats what is left over as a
    # request to place, move or clear a stand-in black dot.
    sim = SimulatedTargetManager(fire, rotate=args.rotate)
    # The flags set the starting point; from here the sliders own these values.
    th = Thresholds(args)
    controls = None
    if not args.headless:
        cv2.namedWindow(_WIN, cv2.WINDOW_AUTOSIZE)
        cv2.setMouseCallback(_WIN, sim.on_mouse)
        if debug:
            th.show()
        try:
            controls = Controls(link)
        except Exception as exc:
            # A missing or broken Tk should cost the panel, not the tracking.
            print(f"controls window unavailable ({exc}); running without it.")

    print("Press 'q' to quit, 'f' or the FIRE button to flash the laser."
          + ("  SPACE/n = next image." if folder_mode else ""))
    try:
        for frame in frames:
            # Read the sliders once, here, so the whole frame is detected with
            # one consistent set of thresholds. No-op while they are hidden.
            th.poll()
            red, red_mask = find_red_dot(frame, *th.red_args())
            targets, black_mask, rejects = find_black_dots(frame, *th.black_args())
            target = pick_target(targets, frame.shape, args.target, red)
            # If detection found no black target, fall back to a simulated one
            # created by a user click.
            sim.update_state(targets, frame.shape)
            if target is None and sim.get_simulated() is not None:
                target = sim.get_simulated()

            dx, dy, valid = error_vector(red, target, frame.shape)
            # Send every frame, valid or not: the firmware treats silence as a
            # dead link (300 ms) and resets its PIDs, while valid=0 only holds.
            on_wire = link.send(dx, dy, valid)
            # Drain every UART line so console traffic cannot fill the OS buffer.
            # Only explicit uplink protocol messages affect the UI.
            for esp_line in link.poll():
                sample = parse_telemetry(esp_line)
                if sample is not None:
                    telemetry = sample
                    telemetry_at = time.monotonic()
                    if sample["arr"]:
                        fire.blink()
                    continue

            # Display-only: how close counts as "on target" for the border
            # colour. Independent of the firmware's own (much tighter) deadzone,
            # which is what actually decides when it reports arrival.
            on_target = valid and math.hypot(dx, dy) <= args.ready_error

            now = time.time()
            fps = 0.9 * fps + 0.1 / max(now - last_t, 1e-6)
            last_t = now
            # Do not leave the last sample on screen after the operator turns
            # telemetry off or the board/link goes away.
            live_telemetry = (telemetry if time.monotonic() - telemetry_at < _TELEMETRY_STALE_S
                              else None)

            if not args.headless:
                view = draw_overlay(frame, red, targets, target,
                                    dx, dy, valid, fps, link, live_telemetry,
                                    rejects if debug else ())
                if args.rotate:
                    view = cv2.rotate(view, _ROTATE[args.rotate])
                cv2.imshow(_WIN, fire.draw(view, on_target))
                if debug:
                    show_masks(red_mask, black_mask, args.rotate)
                raw_key = _wait_key(folder_mode, controls)
                key = raw_key & 0xFF if raw_key != -1 else -1
                try:
                    if sim.handle_key(raw_key):
                        continue
                except Exception:
                    pass
                if key == ord('q'):
                    break
                if key == ord('f'):
                    fire.trigger()
                    link.fire()
                elif fire.take():
                    link.fire()
                # Simulated targets persist across keypresses; right-click in
                # the view clears them.
                if key == ord('d'):
                    debug = not debug
                    if debug:
                        th.show()
                    else:
                        hide_masks()
                        th.hide()
                if key == ord('p'):
                    # Sliders are lost on exit; this is how a session's tuning
                    # becomes the next run's command line.
                    print(th.flags())
            else:
                if args.verbose and on_wire:
                    print(f"E {dx:+.4f} {dy:+.4f} {1 if valid else 0}")
                # Nothing paces a headless still-image run; don't spin a core.
                time.sleep(link.min_interval / 4.0)
    except KeyboardInterrupt:
        pass
    finally:
        link.close()
        if controls is not None:
            controls.close()
        cv2.destroyAllWindows()
    print(f"{link.sent} frames sent.")


def main():
    ap = argparse.ArgumentParser(
        description="Red dot + black target dot -> error vector -> ESP32 UART")
    ap.add_argument("--source", default="cam",
                    help="'cam' (OAK), an image, a folder, a video file, or a "
                         "webcam index")
    ap.add_argument("--width", type=int, default=1280, help="OAK frame width")
    ap.add_argument("--height", type=int, default=720, help="OAK frame height")
    ap.add_argument("--rotate", type=int, default=0, choices=[0, 90, 180, 270],
                    help="rotate the DISPLAY N degrees CCW (detection unaffected)")

    # --- serial link to the ESP32 (docs/uart-protocol.md) ---
    ap.add_argument("--port", default=None, nargs="?", const="auto",
                    help="COM port of the ESP32-S3, e.g. COM5. Bare --port (or "
                         "--port auto) finds the board by USB VID/PID. Omitted "
                         "entirely = detect only, send nothing")
    ap.add_argument("--list-ports", action="store_true",
                    help="list the serial ports, marking the likely board, and exit")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--rate", type=float, default=30.0,
                    help="max frames/second put on the wire (15-30 is the "
                         "protocol's recommended range)")
    ap.add_argument("--echo", action="store_true",
                    help="print every received ESP32 line; console text is "
                         "never shown in the window")

    ap.add_argument("--ready-error", type=float, default=0.02,
                    help="|error| at or below which the FIRE border turns red "
                         "(on target). Display only - the firmware decides "
                         "arrival on its own, tighter deadzone")

    # --- which black dot is the target ---
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
