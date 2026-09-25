"""Collect photos of the red AIM dot and the green target dot for tuning.

The target moved from a printed black dot to a green laser
(`dots.find_green_dot`, wired into `tracker.py` in place of the old
`dots.find_black_dots`), and tuning either detector needs example frames.
This script just points a camera at the rig and saves frames to `dataset/`
on demand -- no detection is applied to what gets saved. The red/green
highlight on screen is a live preview to help frame the shot, not a filter:
what you see boxed is not what gets written to disk.

Saved frames drop straight into `dataset/`, which is also what
`tracker.py --source dataset --debug` reads to step through and tune against
(see tracker.py's folder mode).

Usage:
    py -3 collect_dataset.py                  # live OAK
    py -3 collect_dataset.py --source 0       # any USB webcam, no OAK needed
    py -3 collect_dataset.py --out other_dir

Keys (with the preview window focused): SPACE/s = save frame, m = toggle the
red/green highlight preview, q = quit.
"""

import argparse
import datetime
import glob
import os

import cv2
import numpy as np

_OUT_DEFAULT = os.path.join(os.path.dirname(__file__), "dataset")
_RED = (0, 0, 255)
_GREEN = (0, 220, 0)


def _camera_frames(width, height, fps, focus):
    """Yield BGR frames from the OAK camera (fixed settings, no live tuning).

    Trimmed down from tracker.py's camera_frames(): this tool only ever runs
    for a capture session, so it does not need that one's live fps/queue/focus
    reconfiguration or its restart-on-refusal loop.
    """
    import depthai as dai
    pipeline = dai.Pipeline()
    cam = pipeline.create(dai.node.Camera).build()
    camera_out = cam.requestOutput((width, height), type=dai.ImgFrame.Type.BGR888i,
                                   fps=fps)
    video_queue = camera_out.createOutputQueue(maxSize=1, blocking=False)
    control_queue = cam.inputControl.createInputQueue()
    pipeline.start()
    ctrl = dai.CameraControl()
    ctrl.setAutoFocusMode(dai.CameraControl.AutoFocusMode.OFF)
    ctrl.setManualFocus(int(focus))
    control_queue.send(ctrl)
    print(f"OAK pipeline started: {width}x{height} @ {fps:g} fps.")
    with pipeline:
        while pipeline.isRunning():
            yield video_queue.get().getCvFrame()


def _capture_frames(source):
    """Yield BGR frames from a webcam index or a video file."""
    cap = cv2.VideoCapture(int(source) if str(source).isdigit() else source)
    if not cap.isOpened():
        raise SystemExit(f"Could not open {source}")
    try:
        while True:
            ok, frame = cap.read()
            if not ok:
                return
            yield frame
    finally:
        cap.release()


def _open_source(args):
    if args.source == "cam":
        return _camera_frames(args.width, args.height, args.fps, args.focus)
    return _capture_frames(args.source)


def _next_index(out_dir):
    """Lowest unused save index in `out_dir`, so reruns append, not overwrite."""
    existing = glob.glob(os.path.join(out_dir, "[0-9]" * 4 + "_*.jpg"))
    if not existing:
        return 0
    return max(int(os.path.basename(p)[:4]) for p in existing) + 1


def _highlight(frame):
    """Box the reddest and greenest blobs above a loose floor, for framing only.

    Same R-max(G,B) / G-max(R,B) idea as dots.redness_map, but with no area,
    shape or circularity gate -- this never decides what gets saved, it only
    helps the operator see both dots are in frame before pressing save.
    """
    view = frame.copy()
    b, g, r = cv2.split(frame)
    for chan, color, label in ((cv2.subtract(r, cv2.max(g, b)), _RED, "AIM"),
                               (cv2.subtract(g, cv2.max(r, b)), _GREEN, "target")):
        mask = (chan >= 40).astype(np.uint8) * 255
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        if not contours:
            continue
        blob = max(contours, key=cv2.contourArea)
        if cv2.contourArea(blob) < 2:
            continue
        x, y, w, h = cv2.boundingRect(blob)
        cv2.rectangle(view, (x, y), (x + w, y + h), color, 2)
        cv2.putText(view, label, (x, max(0, y - 6)), cv2.FONT_HERSHEY_SIMPLEX,
                    0.5, color, 1, cv2.LINE_AA)
    return view


def run(args):
    os.makedirs(args.out, exist_ok=True)
    index = _next_index(args.out)
    show_highlight = True
    print(f"Saving into {args.out}, starting at {index:04d}.")
    print("SPACE/s = save, m = toggle highlight preview, q = quit.")
    for frame in _open_source(args):
        view = _highlight(frame) if show_highlight else frame
        cv2.putText(view, f"saved: {index}", (8, 20), cv2.FONT_HERSHEY_SIMPLEX,
                    0.6, (255, 255, 255), 1, cv2.LINE_AA)
        cv2.imshow("collect_dataset", view)
        key = cv2.waitKey(1) & 0xFF
        if key in (ord("q"), 27):
            break
        if key == ord("m"):
            show_highlight = not show_highlight
        elif key in (ord(" "), ord("s")):
            stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
            path = os.path.join(args.out, f"{index:04d}_{stamp}.jpg")
            cv2.imwrite(path, frame)
            print(f"-> {path}")
            index += 1
    cv2.destroyAllWindows()
    print(f"{index} frame(s) in {args.out}.")


def main():
    ap = argparse.ArgumentParser(
        description="Save camera frames for tuning the red AIM / green target detectors")
    ap.add_argument("--source", default="cam",
                    help="'cam' (OAK), a webcam index, or a video file")
    ap.add_argument("--width", type=int, default=1280, help="OAK frame width")
    ap.add_argument("--height", type=int, default=720, help="OAK frame height")
    ap.add_argument("--fps", type=float, default=30.0, help="OAK sensor frame rate")
    ap.add_argument("--focus", type=int, default=130,
                    help="OAK lens position 0..255, autofocus off (see tracker.py)")
    ap.add_argument("--out", default=_OUT_DEFAULT,
                    help="folder to save into (default: dataset/ next to this script)")
    args = ap.parse_args()
    run(args)


if __name__ == "__main__":
    main()
