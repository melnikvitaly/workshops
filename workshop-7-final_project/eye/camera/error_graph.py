"""Tk-native plotting window for tracking error traces."""

import os
import time
from collections import deque

import tkinter as tk
from tkinter import ttk

import matplotlib
matplotlib.use("TkAgg")
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure


_REDRAW_S = 0.2  # repaint at most 5 times a second
_WINDOW = 300     # samples shown while unpinned (the old fixed behaviour)
_CAP = 20000      # samples kept in memory; pinning past this loses the tail

_OK = "#1a7f37"
_ERR = "#b42318"

_SNAPSHOT_DIR = os.path.join(os.path.dirname(__file__), "snapshots")


class ErrorGraphWindow:
    """Embedded Tk plot widget for pan/tilt tracking error history.

    `gains_label` is an optional zero-arg callable returning a short,
    filename-safe string describing the gains currently in effect (see
    `Controls.gains_label` in controls.py), or None before any gains have
    been applied. Kept as a callback rather than a direct Controls
    reference so this module stays free of that dependency.
    """

    def __init__(self, master, gains_label=None):
        self._alive = True
        self._gains_label = gains_label or (lambda: None)
        self.values = {"pan": deque(maxlen=_CAP), "tilt": deque(maxlen=_CAP)}
        self._n = 0          # total samples ever appended
        self._pin_start = 0  # absolute sample index of the left edge, while pinned

        self.frame = tk.Frame(master, bd=1, relief="solid")
        self.frame.pack(fill="both", expand=True, pady=(8, 0))

        self._fig = Figure(figsize=(6.6, 2.8), dpi=100)
        self.ax = self._fig.add_subplot(111)
        self._title = "tracking error over time"
        self.ax.set_title(self._title)
        self.ax.set_xlabel("sample")
        self.ax.set_ylabel("error")
        self.ax.grid(True, alpha=0.3)

        # The lines and legend are made once; _draw() only swaps their data.
        # Rebuilding the axes on every frame cost ~40 ms and capped the whole
        # detection loop below 25 fps.
        (self._pan_line,) = self.ax.plot([], [], label="pan", color="#ff9f1c", linewidth=2)
        (self._tilt_line,) = self.ax.plot([], [], label="tilt", color="#3b82f6", linewidth=2)
        self.ax.legend(loc="upper right")
        self._last_draw = 0.0

        self.canvas = FigureCanvasTkAgg(self._fig, master=self.frame)
        self.canvas.draw()
        self.canvas.get_tk_widget().pack(fill="both", expand=True)

        row = ttk.Frame(self.frame)
        row.pack(fill="x", padx=4, pady=(0, 4))
        self.pinned = tk.BooleanVar(value=False)
        ttk.Checkbutton(row, text="Pin left edge", variable=self.pinned,
                       command=self._pin_changed).pack(side="left")
        ttk.Button(row, text="Snapshot", command=self._snapshot).pack(
            side="left", padx=(8, 0))
        self._status = ttk.Label(row, text="", foreground=_OK)
        self._status.pack(side="left", padx=(8, 0))

        self._draw()

    def _on_close(self):
        self._alive = False
        self.frame.destroy()

    def _pin_changed(self):
        if self.pinned.get():
            # Freeze at today's left edge -- the window currently on screen.
            shown = min(_WINDOW, self._n)
            self._pin_start = self._n - shown
        self._draw()

    def append(self, dx, dy):
        if not self._alive:
            return
        self.values["pan"].append(dx)
        self.values["tilt"].append(dy)
        self._n += 1
        # Every sample is kept; only the repaint is throttled.
        now = time.monotonic()
        if now - self._last_draw >= _REDRAW_S:
            self._last_draw = now
            self._draw()

    def _window(self):
        """(x_start, pan, tilt) for what should currently be on screen."""
        pan_all = list(self.values["pan"])
        tilt_all = list(self.values["tilt"])
        total = len(pan_all)
        left_absolute = self._n - total  # oldest sample still in the deques

        if self.pinned.get():
            start = max(self._pin_start, left_absolute)
        else:
            start = self._n - min(_WINDOW, total)
        offset = start - left_absolute
        return start, pan_all[offset:], tilt_all[offset:]

    def _draw(self):
        if not self._alive:
            return
        start, pan, tilt = self._window()
        end = start + max(len(pan), len(tilt))
        self._pan_line.set_data(range(start, start + len(pan)), pan)
        self._tilt_line.set_data(range(start, start + len(tilt)), tilt)
        self.ax.set_xlim(start, max(end, start + 1))
        if pan or tilt:
            peak = max((max(abs(v) for v in pan) if pan else 0.0),
                       (max(abs(v) for v in tilt) if tilt else 0.0),
                       0.25)
            self.ax.set_ylim(-peak * 1.2, peak * 1.2)
        self.canvas.draw_idle()

    def _snapshot(self):
        """Save the plot as-is to eye/camera/snapshots/, named with the gains
        that were last applied so a run can be told apart from another."""
        label = self._gains_label() or "gains-unset"
        stamp = time.strftime("%Y%m%d-%H%M%S")
        name = f"error_{stamp}_{label}.png"
        try:
            os.makedirs(_SNAPSHOT_DIR, exist_ok=True)
            path = os.path.join(_SNAPSHOT_DIR, name)
            # Stamp the gains + date onto the saved image itself, then put
            # the plain title back -- the live view does not need either.
            self.ax.set_title(f"{self._title}\n{label} · {stamp}", fontsize=10)
            self._fig.savefig(path, dpi=150)
            self.ax.set_title(self._title)
            self.canvas.draw_idle()
        except OSError as exc:
            self._status.config(text=f"snapshot failed: {exc}", foreground=_ERR)
            return
        self._status.config(text=f"saved {name}", foreground=_OK)

    def pump(self):
        if self._alive:
            try:
                self.frame.update()
            except tk.TclError:
                self._alive = False

    def close(self):
        if not self._alive:
            return
        self._on_close()
