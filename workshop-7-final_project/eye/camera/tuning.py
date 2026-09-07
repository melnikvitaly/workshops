"""Live threshold sliders for the detection stage.

The `--red-*` and `--black-*` numbers decide what counts as a dot, and finding
them is a matter of watching the masks while moving one at a time. As command
line flags that costs a restart per guess; here they are HighGUI trackbars
sitting next to the `--debug` mask window, so a value can be swept against a
live frame.

Trackbars are integer-only and always start at zero, so each parameter carries
the affine map back to its real value:

    pos = (value - lo) * scale        value = lo + pos / scale

`scale` is slider steps per unit: 100 for a 0..1 fraction, 1 for a pixel count,
and below 1 for a range too wide to drag through one step at a time.

Nothing here is written back to `args`. `Thresholds` owns the live values, the
detection calls read them, and `flags()` prints the command line that would
reproduce the current set - otherwise a good tuning session dies with the
window.
"""

from collections import namedtuple

import cv2
import numpy as np

# attr:  the argparse destination, which is also the attribute on Thresholds
# label: the trackbar's name, unique within its window and short enough to read
# cast:  int for counts and pixels, float for ratios
_Param = namedtuple("_Param", "attr label lo hi scale cast")

RED_WIN = "tune: red dot"
BLACK_WIN = "tune: black dots"

_RED_PARAMS = (
    _Param("red_rel", "rel /100", 0.0, 1.0, 100, float),
    _Param("red_min_redness", "min redness", 0, 120, 1, int),
    _Param("red_circ", "circ /100", 0.0, 1.0, 100, float),
    _Param("red_area_min", "area min", 0, 200, 1, int),
    _Param("red_area_max", "area max x10", 0, 5000, 0.1, int),
)

_BLACK_PARAMS = (
    # is it ink?
    _Param("black_offset", "offset", 0, 60, 1, int),
    _Param("black_block", "block px", 3, 201, 1, int),
    _Param("black_darkness", "darkness /100", 0.0, 1.5, 100, float),
    _Param("black_sat_margin", "sat margin", 0, 255, 1, int),
    # is it a circle?
    _Param("black_circ", "circ /100", 0.0, 1.0, 100, float),
    _Param("black_radial", "radial /1000", 0.0, 0.5, 1000, float),
    _Param("black_aspect", "aspect /100", 1.0, 3.0, 100, float),
    _Param("black_solidity", "solidity /100", 0.0, 1.0, 100, float),
    _Param("black_compact", "compact /100", 0.0, 1.0, 100, float),
    _Param("black_hole", "hole /100", 0.0, 1.0, 100, float),
    # size and framing
    _Param("black_area_min", "area min", 0, 2000, 1, int),
    _Param("black_area_max", "area max x100", 0, 50000, 0.01, int),
    _Param("black_edge_margin", "edge margin", -1, 50, 1, int),
)

_WINDOWS = ((RED_WIN, _RED_PARAMS), (BLACK_WIN, _BLACK_PARAMS))


def _count(p):
    """Trackbar range: 0 .. count maps onto lo .. hi."""
    return max(1, int(round((p.hi - p.lo) * p.scale)))


def _to_pos(p, value):
    return max(0, min(_count(p), int(round((value - p.lo) * p.scale))))


def _to_value(p, pos):
    return p.cast(p.lo + pos / p.scale)


def _banner(text, width=430):
    """The window needs an image to size itself around; make it say something."""
    img = np.full((30, width, 3), 40, np.uint8)
    cv2.putText(img, text, (10, 21), cv2.FONT_HERSHEY_SIMPLEX, 0.55,
                (220, 220, 220), 1, cv2.LINE_AA)
    return img


class Thresholds:
    """The detection parameters, as live values with an optional slider UI.

    Constructed from the parsed command line, so the flags stay the way to set
    a starting point and the sliders are how it is refined.
    """

    def __init__(self, args):
        for _, params in _WINDOWS:
            for p in params:
                setattr(self, p.attr, getattr(args, p.attr))
        self._open = False

    @property
    def open(self):
        return self._open

    def show(self):
        """Open the two slider windows, seeded with the current values."""
        if self._open:
            return
        for win, params in _WINDOWS:
            cv2.namedWindow(win, cv2.WINDOW_AUTOSIZE)
            cv2.imshow(win, _banner(win))
            for p in params:
                # The callback fires on the HighGUI thread and would race the
                # frame it is meant to affect; poll() reads the positions once
                # per frame instead, so a whole sweep lands on one detection.
                cv2.createTrackbar(p.label, win, _to_pos(p, getattr(self, p.attr)),
                                   _count(p), lambda _pos: None)
        self._open = True

    def hide(self):
        """Close the slider windows. The values stay as they were left."""
        for win, _ in _WINDOWS:
            try:
                cv2.destroyWindow(win)
            except cv2.error:
                pass
        self._open = False

    def poll(self):
        """Read every slider into the matching attribute. Call once per frame."""
        if not self._open:
            return
        for win, params in _WINDOWS:
            for p in params:
                try:
                    pos = cv2.getTrackbarPos(p.label, win)
                except cv2.error:
                    # The window was closed with its X rather than by 'd'.
                    self._open = False
                    return
                setattr(self, p.attr, _to_value(p, pos))

    def red_args(self):
        """Positional arguments for dots.find_red_dot, after the frame."""
        return (self.red_area_min, self.red_area_max, self.red_circ,
                self.red_min_redness, self.red_rel)

    def black_args(self):
        """Positional arguments for dots.find_black_dots, after the frame."""
        return (self.black_area_min, self.black_area_max, self.black_circ,
                self.black_darkness, self.black_sat_margin, self.black_block,
                self.black_offset, self.black_radial, self.black_aspect,
                self.black_solidity, self.black_compact, self.black_hole,
                self.black_edge_margin)

    def flags(self):
        """The current values as a command line, to carry a tuning session out."""
        out = []
        for _, params in _WINDOWS:
            for p in params:
                value = getattr(self, p.attr)
                text = f"{value:g}" if p.cast is float else str(value)
                out.append(f"--{p.attr.replace('_', '-')} {text}")
        return " ".join(out)
