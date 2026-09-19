"""Tk-native plotting window for tracking error traces."""

import time
from collections import deque

import tkinter as tk

import matplotlib
matplotlib.use("TkAgg")
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure


_REDRAW_S = 0.2  # repaint at most 5 times a second


class ErrorGraphWindow:
    """Embedded Tk plot widget for pan/tilt tracking error history."""

    def __init__(self, master):
        self._alive = True
        self.values = {"pan": deque(maxlen=300), "tilt": deque(maxlen=300)}

        self.frame = tk.Frame(master, bd=1, relief="solid")
        self.frame.pack(fill="both", expand=True, pady=(8, 0))

        fig = Figure(figsize=(6.6, 2.8), dpi=100)
        self.ax = fig.add_subplot(111)
        self.ax.set_title("tracking error over time")
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

        self.canvas = FigureCanvasTkAgg(fig, master=self.frame)
        self.canvas.draw()
        self.canvas.get_tk_widget().pack(fill="both", expand=True)
        self._draw()

    def _on_close(self):
        self._alive = False
        self.frame.destroy()

    def append(self, dx, dy):
        if not self._alive:
            return
        self.values["pan"].append(dx)
        self.values["tilt"].append(dy)
        # Every sample is kept; only the repaint is throttled.
        now = time.monotonic()
        if now - self._last_draw >= _REDRAW_S:
            self._last_draw = now
            self._draw()

    def _draw(self):
        if not self._alive:
            return
        pan = list(self.values["pan"])
        tilt = list(self.values["tilt"])
        self._pan_line.set_data(range(len(pan)), pan)
        self._tilt_line.set_data(range(len(tilt)), tilt)
        self.ax.set_xlim(0, max(len(pan), len(tilt), 1))
        if pan or tilt:
            peak = max((max(abs(v) for v in pan) if pan else 0.0),
                       (max(abs(v) for v in tilt) if tilt else 0.0),
                       0.25)
            self.ax.set_ylim(-peak * 1.2, peak * 1.2)
        self.canvas.draw_idle()

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
