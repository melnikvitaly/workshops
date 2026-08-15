"""Tk-native plotting window for tracking error traces."""

from collections import deque

import tkinter as tk

import matplotlib
matplotlib.use("TkAgg")
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure


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
        self._draw()

    def _draw(self):
        if not self._alive:
            return
        self.ax.clear()
        self.ax.set_title("tracking error over time")
        self.ax.set_xlabel("sample")
        self.ax.set_ylabel("error")
        self.ax.grid(True, alpha=0.3)

        pan = list(self.values["pan"])
        tilt = list(self.values["tilt"])
        if pan:
            self.ax.plot(range(len(pan)), pan, label="pan", color="#ff9f1c", linewidth=2)
        if tilt:
            self.ax.plot(range(len(tilt)), tilt, label="tilt", color="#3b82f6", linewidth=2)

        self.ax.legend(loc="upper right")
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
