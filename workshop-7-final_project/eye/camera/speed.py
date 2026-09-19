"""Live speed settings: camera fps, frame queue depth, and the send rate.

The `--fps`, `--queue-size` and `--rate` flags set the starting point; the
"Speed" box in the right panel (see app_window.py) changes them while running.

| Setting      | Applied                                                           |
|--------------|-------------------------------------------------------------------|
| `rate`       | at once (`ErrorLink.set_rate`)                                    |
| `queue_size` | at once if the camera allows it, else the camera restarts         |
| `fps`        | the camera pipeline restarts (about a second, the view freezes)  |

`SpeedSettings` only holds the numbers. `detect_dots.camera_frames` reads
`fps` and `queue_size` every frame and reacts when they change.
"""

import tkinter as tk
from tkinter import ttk

FPS_RANGE = (5, 120)
QUEUE_RANGE = (1, 8)
RATE_RANGE = (1, 100)


class SpeedSettings:
    """The live speed values, with a small panel to change them."""

    def __init__(self, args, link):
        self.fps = float(args.fps)
        self.queue_size = int(args.queue_size)
        self.rate = float(args.rate)
        self._link = link

    def build(self, parent):
        """The 'Speed' box. Returns the widget."""
        box = ttk.LabelFrame(parent, text="Speed", padding=6)
        box.columnconfigure(1, weight=1)

        self._vars = {
            "fps": tk.StringVar(value=f"{self.fps:g}"),
            "queue_size": tk.StringVar(value=str(self.queue_size)),
            "rate": tk.StringVar(value=f"{self.rate:g}"),
        }
        rows = (
            ("Camera fps (restarts camera)", "fps", FPS_RANGE),
            ("Frame queue (1 = newest)", "queue_size", QUEUE_RANGE),
            ("Send rate, Hz", "rate", RATE_RANGE),
        )
        for row, (label, name, (lo, hi)) in enumerate(rows):
            ttk.Label(box, text=label).grid(row=row, column=0, sticky="w", pady=2)
            spin = ttk.Spinbox(box, from_=lo, to=hi, width=6,
                               textvariable=self._vars[name])
            spin.grid(row=row, column=1, sticky="e")
            spin.bind("<Return>", lambda _e: self.apply())

        bar = ttk.Frame(box)
        bar.grid(row=len(rows), column=0, columnspan=2, sticky="ew", pady=(6, 0))
        ttk.Button(bar, text="Apply", command=self.apply).pack(side="left")
        self._status = ttk.Label(bar, text="")
        self._status.pack(side="left", padx=8)
        return box

    def apply(self):
        """Read the fields, range-check them, and publish. Bad input is refused."""
        try:
            fps = float(self._vars["fps"].get())
            queue = int(self._vars["queue_size"].get())
            rate = float(self._vars["rate"].get())
        except ValueError:
            self._say("numbers only", ok=False)
            return
        for name, value, (lo, hi) in (("fps", fps, FPS_RANGE),
                                      ("queue", queue, QUEUE_RANGE),
                                      ("rate", rate, RATE_RANGE)):
            if not lo <= value <= hi:
                self._say(f"{name} must be {lo}..{hi}", ok=False)
                return
        self.fps, self.queue_size, self.rate = fps, queue, rate
        self._link.set_rate(rate)
        self._say("applied", ok=True)

    def refused(self, fps, queue_size, why):
        """The camera would not take the requested setting: show the one in use."""
        self.fps, self.queue_size = float(fps), int(queue_size)
        if hasattr(self, "_vars"):
            self._vars["fps"].set(f"{self.fps:g}")
            self._vars["queue_size"].set(str(self.queue_size))
            self._say(f"{why}; kept {self.fps:g} fps", ok=False)

    def _say(self, text, ok):
        self._status.config(text=text, foreground="#1a7f37" if ok else "#b42318")
