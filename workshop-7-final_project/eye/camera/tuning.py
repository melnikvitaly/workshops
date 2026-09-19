"""Live threshold sliders for the detection stage.

The `--red-*` and `--black-*` numbers decide what counts as a dot, and finding
them is a matter of watching the masks while moving one at a time. As command
line flags that costs a restart per guess; here they are sliders in the right
panel of the main window (see app_window.py), so a value can be swept against a
live frame.

Each parameter carries an affine map between the slider position and its real
value, kept from the days when these were integer-only HighGUI trackbars:

    pos = (value - lo) * scale        value = lo + pos / scale

`scale` is slider steps per unit: 100 for a 0..1 fraction, 1 for a pixel count,
and below 1 for a range too wide to drag through one step at a time. It is now
just the slider's step size (1 / scale).

Nothing here is written back to `args`. `Thresholds` owns the live values, the
detection calls read them, and `flags()` prints the command line that would
reproduce the current set - otherwise a good tuning session dies with the
window.
"""

import tkinter as tk
from collections import namedtuple
from tkinter import ttk

# attr:  the argparse destination, which is also the attribute on Thresholds
# label: the slider's caption, short enough to read
# cast:  int for counts and pixels, float for ratios
_Param = namedtuple("_Param", "attr label lo hi scale cast")

_RED_PARAMS = (
    _Param("red_rel", "rel", 0.0, 1.0, 100, float),
    _Param("red_min_redness", "min redness", 0, 120, 1, int),
    _Param("red_circ", "circ", 0.0, 1.0, 100, float),
    _Param("red_area_min", "area min", 0, 200, 1, int),
    _Param("red_area_max", "area max", 0, 5000, 0.1, int),
)

_BLACK_PARAMS = (
    # is it ink?
    _Param("black_offset", "offset", 0, 60, 1, int),
    _Param("black_block", "block px", 3, 201, 1, int),
    _Param("black_darkness", "darkness", 0.0, 1.5, 100, float),
    _Param("black_sat_margin", "sat margin", 0, 255, 1, int),
    # is it a circle?
    _Param("black_circ", "circ", 0.0, 1.0, 100, float),
    _Param("black_radial", "radial", 0.0, 0.5, 1000, float),
    _Param("black_aspect", "aspect", 1.0, 3.0, 100, float),
    _Param("black_solidity", "solidity", 0.0, 1.0, 100, float),
    _Param("black_compact", "compact", 0.0, 1.0, 100, float),
    _Param("black_hole", "hole", 0.0, 1.0, 100, float),
    # size and framing
    _Param("black_area_min", "area min", 0, 2000, 1, int),
    _Param("black_area_max", "area max", 0, 50000, 0.01, int),
    _Param("black_edge_margin", "edge margin", -1, 50, 1, int),
)

_TABS = (("Red dot", _RED_PARAMS), ("Black dots", _BLACK_PARAMS))


def _count(p):
    """Slider range: 0 .. count maps onto lo .. hi."""
    return max(1, int(round((p.hi - p.lo) * p.scale)))


def _to_pos(p, value):
    return max(0, min(_count(p), int(round((value - p.lo) * p.scale))))


def _to_value(p, pos):
    return p.cast(p.lo + pos / p.scale)


class Thresholds:
    """The detection parameters, as live values with a slider panel.

    Constructed from the parsed command line, so the flags stay the way to set
    a starting point and the sliders are how it is refined. The slider
    callbacks run inside Tk's event loop on the main thread, which is the same
    thread that detects, so a frame always sees one consistent set.
    """

    def __init__(self, args):
        for _, params in _TABS:
            for p in params:
                setattr(self, p.attr, getattr(args, p.attr))

    def build(self, parent):
        """The slider notebook, one tab per detector. Returns the widget."""
        book = ttk.Notebook(parent)
        for title, params in _TABS:
            page = ttk.Frame(book, padding=6)
            page.columnconfigure(1, weight=1)
            for row, p in enumerate(params):
                self._add_slider(page, row, p)
            book.add(page, text=title)
        return book

    def _add_slider(self, page, row, p):
        pos = tk.DoubleVar(value=_to_pos(p, getattr(self, p.attr)))
        text = tk.StringVar(value=f"{getattr(self, p.attr):g}")

        def changed(_raw):
            # Snap to the parameter's step, then publish.
            snapped = int(round(pos.get()))
            value = _to_value(p, snapped)
            setattr(self, p.attr, value)
            text.set(f"{value:g}")

        ttk.Label(page, text=p.label).grid(row=row, column=0, sticky="w", pady=2)
        ttk.Scale(page, from_=0, to=_count(p), variable=pos,
                  command=changed).grid(row=row, column=1, sticky="ew", padx=6)
        ttk.Label(page, textvariable=text, width=7, anchor="e").grid(row=row, column=2)

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
        for _, params in _TABS:
            for p in params:
                value = getattr(self, p.attr)
                text = f"{value:g}" if p.cast is float else str(value)
                out.append(f"--{p.attr.replace('_', '-')} {text}")
        return " ".join(out)
