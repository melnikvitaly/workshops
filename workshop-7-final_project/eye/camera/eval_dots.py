"""Score find_red_dot / find_green_dot against the labelled dataset/ folders.

dataset/ is organised as:

    dataset/only-red-dot/      red laser on,   green off
    dataset/only-green-dot/    green laser on, red off
    dataset/both-red-and-green/  both on

The folder name is the ground truth for whether each detector should fire.
This never opens a window: it runs every image once and prints a pass/fail
table plus a confusion summary, so thresholds in dots.py can be iterated
quickly from the command line. Pass --save-annotated to also write boxed
copies for a visual pass over the failures.

Usage:
    py -3 eval_dots.py
    py -3 eval_dots.py --save-annotated review
"""

import argparse
import glob
import os

import cv2

from dots import find_green_dot, find_red_dot

_FOLDERS = {
    "only-red-dot": (True, False),
    "only-green-dot": (False, True),
    "both-red-and-green": (True, True),
}
_DATASET_DEFAULT = os.path.join(os.path.dirname(__file__), "dataset")
_RED = (0, 0, 255)
_GREEN = (0, 220, 0)


def _iter_images(dataset_dir):
    for folder, expect in _FOLDERS.items():
        paths = sorted(glob.glob(os.path.join(dataset_dir, folder, "*.jpg")))
        for p in paths:
            yield folder, p, expect


def run(args):
    total = 0
    counts = {"red_tp": 0, "red_fp": 0, "red_fn": 0, "red_tn": 0,
             "green_tp": 0, "green_fp": 0, "green_fn": 0, "green_tn": 0}
    failures = []

    for folder, path, (want_red, want_green) in _iter_images(args.dataset):
        frame = cv2.imread(path)
        if frame is None:
            print(f"unreadable: {path}")
            continue
        total += 1
        red, red_mask = find_red_dot(frame, *args.red_args())
        green, green_mask = find_green_dot(frame, *args.green_args())
        got_red, got_green = red is not None, green is not None

        for name, want, got in (("red", want_red, got_red), ("green", want_green, got_green)):
            if want and got:
                counts[f"{name}_tp"] += 1
            elif want and not got:
                counts[f"{name}_fn"] += 1
            elif not want and got:
                counts[f"{name}_fp"] += 1
            else:
                counts[f"{name}_tn"] += 1

        ok = got_red == want_red and got_green == want_green
        tag = "ok  " if ok else "FAIL"
        red_note = f"red={'y' if got_red else 'n'}"
        if got_red:
            red_note += f"@{red.center}"
        green_note = f"green={'y' if got_green else 'n'}"
        if got_green:
            green_note += f"@{green.center}"
        print(f"{tag} {folder:20s} {os.path.basename(path):28s} {red_note:18s} {green_note}")
        if not ok:
            failures.append((folder, path, frame, red, green))

    print()
    print(f"{total} images.")
    for name in ("red", "green"):
        tp, fp, fn, tn = (counts[f"{name}_{k}"] for k in ("tp", "fp", "fn", "tn"))
        print(f"  {name}: {tp} correct-hit, {tn} correct-miss, "
             f"{fp} FALSE POSITIVE, {fn} FALSE NEGATIVE")

    if args.save_annotated:
        os.makedirs(args.save_annotated, exist_ok=True)
        for folder, path, frame, red, green in failures:
            view = frame.copy()
            if red is not None:
                cv2.circle(view, red.center, red.radius + 4, _RED, 2)
            if green is not None:
                cv2.circle(view, green.center, green.radius + 4, _GREEN, 2)
            out = os.path.join(args.save_annotated,
                               f"{folder}__{os.path.basename(path)}")
            cv2.imwrite(out, view)
        print(f"\n{len(failures)} failing frame(s) annotated -> {args.save_annotated}/")


class _Args:
    """Bundles the CLI flags into the same *args tuples dots.py expects."""

    def __init__(self, ns):
        self.__dict__.update(vars(ns))

    def red_args(self):
        return (self.red_area_min, self.red_area_max, self.red_circ,
                self.red_min_redness, self.red_rel)

    def green_args(self):
        return (self.green_area_min, self.green_area_max, self.green_circ,
                self.green_min_greenness, self.green_rel)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--dataset", default=_DATASET_DEFAULT)
    ap.add_argument("--save-annotated", metavar="DIR", default=None,
                    help="write boxed copies of every FAILING frame into DIR")

    ap.add_argument("--red-area-min", type=int, default=3)
    ap.add_argument("--red-area-max", type=int, default=2000)
    ap.add_argument("--red-circ", type=float, default=0.35)
    ap.add_argument("--red-min-redness", type=int, default=22)
    ap.add_argument("--red-rel", type=float, default=0.5)

    ap.add_argument("--green-area-min", type=int, default=1)
    ap.add_argument("--green-area-max", type=int, default=2000)
    ap.add_argument("--green-circ", type=float, default=0.20)
    ap.add_argument("--green-min-greenness", type=int, default=18)
    ap.add_argument("--green-rel", type=float, default=0.5)

    run(_Args(ap.parse_args()))


if __name__ == "__main__":
    main()
