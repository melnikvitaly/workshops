"""The controls window: gain presets, manual gains, nudge, telemetry, query.

This is a Tk window, not an OpenCV one. HighGUI has no widgets in the build
these wheels ship (`GUI: WIN32UI`, so `cv2.createButton` raises), which meant
the previous version painted its own buttons into an image and matched clicks
against their *labels* - a preset named "Tiny PD" and the "T: telemetry" action
both answered to the same test - and read text one `waitKey` character at a
time, with no caret and no backspace beyond the buffer.

Tk is in the standard library, so this costs no dependency and gets real
buttons and real entry fields. It does not run its own `mainloop`: `pump()` is
called once per frame from the detect_dots loop, which keeps every callback on
the main thread. That matters because the callbacks write to the serial link
the loop is also using - one thread means no lock.

The camera view stays in OpenCV: it is an image with annotations drawn in image
coordinates, which is what cv2 drawing is for.
"""

import os
import sys
import tkinter as tk
from tkinter import ttk

if __package__ in (None, ""):
    sys.path.insert(0, os.path.dirname(__file__))

from error_graph import ErrorGraphWindow

# Preset gain combinations. (pan_kp, pan_ki, pan_kd), (tilt...)
#
# Weighted towards D, because that is the term worth sweeping on this rig: the
# gimbal overshoots and rings, which D fixes and I does not. I is deliberately
# rare - it is only interesting as the fix for the steady-state offset the PD
# rows leave standing, and as the thing that winds up in Runaway. Tilt runs
# slightly softer than pan throughout because it works against gravity.
#
# Four groups, in grid order: one term at a time -> two baselines -> the PD
# ladder (same P, rising D) -> PD at other P levels.
PRESETS = [
    # --- one term at a time: what each does on its own ---
    {"name": "All Zero", "pan": (0.0, 0.0, 0.0), "tilt": (0.0, 0.0, 0.0)},
    {"name": "P-only", "pan": (50.0, 0.0, 0.0), "tilt": (45.0, 0.0, 0.0)},
    {"name": "I-only", "pan": (0.0, 6.0, 0.0), "tilt": (0.0, 6.0, 0.0)},
    {"name": "D-only", "pan": (0.0, 0.0, 6.0), "tilt": (0.0, 0.0, 5.0)},

    # --- baselines: the firmware default, and it plus D ---
    {"name": "Default (PI)", "pan": (40.0, 4.0, 0.0), "tilt": (35.0, 4.0, 0.0)},
    {"name": "Full PID", "pan": (40.0, 4.0, 6.0), "tilt": (35.0, 4.0, 5.0)},

    # --- PD ladder: Default's P, no I, D climbing. Run these in order against
    # Default (PI) to see D trade overshoot for a standing offset, then Full
    # PID to see I close that offset back up.
    {"name": "PD Light", "pan": (40.0, 0.0, 3.0), "tilt": (35.0, 0.0, 2.5)},
    {"name": "PD", "pan": (40.0, 0.0, 6.0), "tilt": (35.0, 0.0, 5.0)},
    {"name": "PD Heavy", "pan": (40.0, 0.0, 12.0), "tilt": (35.0, 0.0, 10.0)},
    {"name": "PD Sluggish", "pan": (25.0, 0.0, 10.0), "tilt": (22.0, 0.0, 8.0)},

    # --- the same PD shape at other P levels ---
    {"name": "Aggressive PD", "pan": (80.0, 0.0, 10.0), "tilt": (70.0, 0.0, 9.0)},
    {"name": "Soft PD", "pan": (10.0, 0.0, 2.0), "tilt": (8.0, 0.0, 1.6)},
    {"name": "Tiny PD", "pan": (1.0, 0.0, 0.3), "tilt": (1.0, 0.0, 0.25)},

    # Deliberately unstable: huge P with an integrator and nothing to damp it.
    # Keep it last - it is the "what does windup look like" demo, not a tuning
    # candidate.
    {"name": "Runaway", "pan": (500.0, 200.0, 0.0), "tilt": (500.0, 200.0, 0.0)},
]

_AXES = {"pan": "p", "tilt": "t", "both": "b"}

_OK = "#1a7f37"
_ERR = "#b42318"


class Controls:
    """The second window. Build it, then call `pump()` once per frame."""

    def __init__(self, link):
        self.link = link
        self._alive = True

        self.root = tk.Tk()
        self.root.title("gimbal controls")
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        # Off to the side, so it does not open on top of the camera view.
        self.root.geometry("+40+40")

        # Matches config::LOG_TELEMETRY on the firmware side, so the checkbox
        # describes the board's actual state at startup rather than contradicting
        # it. Unticking it sends 'T 0' as usual.
        self.telemetry_on = tk.BooleanVar(value=True)
        self.axis = tk.StringVar(value="both")
        self.gains = {k: tk.StringVar(value=v)
                      for k, v in (("KP", "40"), ("KI", "4"), ("KD", "6"))}
        self.nudge = {k: tk.StringVar(value="5") for k in ("dpan", "dtilt")}

        outer = ttk.Frame(self.root, padding=8)
        outer.pack(fill="both", expand=True)
        self._build_link_row(outer)
        self._build_gains(outer)
        self._build_nudge(outer)
        self._build_presets(outer)

        self.status = ttk.Label(outer, text="ready", foreground=_OK,
                                wraplength=430, justify="left")
        self.status.pack(fill="x", pady=(8, 0))

        self.error_graph = ErrorGraphWindow(outer)
        self.error_graph.frame.pack(fill="both", expand=True)

    # --- construction ------------------------------------------------------

    def _build_link_row(self, parent):
        row = ttk.Frame(parent)
        row.pack(fill="x")
        ttk.Button(row, text="Query gains", command=self._query).pack(side="left")
        ttk.Checkbutton(row, text="Telemetry", variable=self.telemetry_on,
                        command=self._telemetry).pack(side="left", padx=(8, 0))

    def _build_gains(self, parent):
        box = ttk.LabelFrame(parent, text="PID gains", padding=6)
        box.pack(fill="x", pady=(8, 0))
        ttk.Combobox(box, textvariable=self.axis, width=6, state="readonly",
                     values=list(_AXES)).grid(row=0, column=0, padx=(0, 8))
        for i, (name, var) in enumerate(self.gains.items()):
            ttk.Label(box, text=name).grid(row=0, column=1 + 2 * i)
            ttk.Entry(box, textvariable=var, width=7).grid(
                row=0, column=2 + 2 * i, padx=(2, 8))
        ttk.Button(box, text="Set", command=self._set_gains).grid(row=0, column=7)

    def _build_nudge(self, parent):
        box = ttk.LabelFrame(parent, text="Nudge physical gimbal (open loop, degrees)", padding=6)
        box.pack(fill="x", pady=(8, 0))
        for i, (name, var) in enumerate(self.nudge.items()):
            ttk.Label(box, text=name).grid(row=0, column=2 * i)
            ttk.Entry(box, textvariable=var, width=7).grid(
                row=0, column=1 + 2 * i, padx=(2, 8))
        ttk.Button(box, text="Nudge", command=self._do_nudge).grid(row=0, column=4)
        ttk.Label(box, text=("Applies a known physical kick -- like a bump, vibration, "
                             "wind gust, or servo slip -- so the loop can correct it.")).grid(
                                 row=1, column=0, columnspan=5, sticky="w", pady=(4, 0))

    def _build_presets(self, parent):
        box = ttk.LabelFrame(parent, text="Presets", padding=6)
        box.pack(fill="x", pady=(8, 0))

        self.preset_var = tk.StringVar(value=PRESETS[0]["name"])
        self.preset_names = [p["name"] for p in PRESETS]
        ttk.OptionMenu(box, self.preset_var, self.preset_var.get(),
                       *self.preset_names).grid(row=0, column=0,
                                                sticky="ew", padx=(0, 8))
        ttk.Button(box, text="Apply", command=self._apply_selected_preset).grid(
            row=0, column=1, sticky="e")
        box.columnconfigure(0, weight=1)

    # --- actions -----------------------------------------------------------

    def _say(self, text, ok=True):
        self.status.config(text=text, foreground=_OK if ok else _ERR)

    def _floats(self, variables):
        """Parse entry fields, naming the offending one if it does not parse."""
        out = []
        for name, var in variables.items():
            try:
                out.append(float(var.get()))
            except ValueError:
                raise ValueError(f"{name} is not a number: {var.get()!r}")
        return out

    def _query(self):
        try:
            self.link.query()
            self._say("Q sent - the reply is the 'esp32 |' line on the view")
        except Exception as exc:
            self._say(f"query failed: {exc}", ok=False)

    def _telemetry(self):
        on = self.telemetry_on.get()
        try:
            self.link.telemetry(1 if on else 0)
            self._say(f"telemetry {'ON' if on else 'OFF'}")
        except Exception as exc:
            # Put the checkbox back: it should show what the firmware was told.
            self.telemetry_on.set(not on)
            self._say(f"telemetry failed: {exc}", ok=False)

    def _set_gains(self):
        try:
            kp, ki, kd = self._floats(self.gains)
            axis = _AXES[self.axis.get()]
            self.link.set_gains(axis, kp, ki, kd)
            self._say(f"gains {self.axis.get()} {kp:g}, {ki:g}, {kd:g}")
        except Exception as exc:
            self._say(f"set gains failed: {exc}", ok=False)

    def _do_nudge(self):
        try:
            dpan, dtilt = self._floats(self.nudge)
            self.link.nudge(dpan, dtilt)
            self._say(f"nudge {dpan:g}, {dtilt:g}")
        except Exception as exc:
            self._say(f"nudge failed: {exc}", ok=False)

    def _apply_selected_preset(self):
        preset_name = self.preset_var.get()
        preset = next(p for p in PRESETS if p["name"] == preset_name)
        try:
            self.link.set_gains("p", *preset["pan"])
            self.link.set_gains("t", *preset["tilt"])
        except Exception as exc:
            self._say(f"preset {preset['name']} failed: {exc}", ok=False)
            return
        # Leave the manual fields showing what is actually loaded, so a preset
        # can be nudged by hand from where it landed.
        for name, value in zip(("KP", "KI", "KD"), preset["pan"]):
            self.gains[name].set(f"{value:g}")
        self.axis.set("pan")
        self._say(f"preset {preset['name']} applied")

    # --- the loop's end ----------------------------------------------------

    def record_error(self, dx, dy):
        """Append a sample to the separate error history chart."""
        if self.error_graph is not None and getattr(self.error_graph, "_alive", False):
            self.error_graph.append(dx, dy)

    def pump(self):
        """Service Tk's event queue. False once the window has been closed.

        `update`, not `mainloop`: the detect_dots loop owns the thread and this
        borrows it for as long as the queued callbacks take.
        """
        if not self._alive:
            return False
        try:
            self.root.update()
            if self.error_graph is not None:
                self.error_graph.pump()
        except tk.TclError:
            self._alive = False
        return self._alive

    def _on_close(self):
        self.close()
        print("controls window closed; the camera view keeps running.")

    def close(self):
        if not self._alive:
            return
        self._alive = False
        try:
            if self.error_graph is not None:
                self.error_graph.close()
            self.root.destroy()
        except tk.TclError:
            pass
