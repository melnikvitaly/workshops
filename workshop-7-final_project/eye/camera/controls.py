"""The controls panel: PID gain table with presets, nudge, zone, telemetry, query.

This is a Tk frame, not an OpenCV one. HighGUI has no widgets in the build
these wheels ship (`GUI: WIN32UI`, so `cv2.createButton` raises), which meant
an earlier version painted its own buttons into an image and matched clicks
against their *labels*, and read text one `waitKey` character at a time.

It is the left panel of the main window (app_window.py), which owns the Tk
root and calls `pump()` once per frame from the tracker loop. Callbacks
run on the main thread and only queue lines on the serial link; the link's own
worker thread does the actual port I/O (see serial_link.ErrorLink).
"""

import os
import sys
import tkinter as tk
from tkinter import ttk

if __package__ in (None, ""):
    sys.path.insert(0, os.path.dirname(__file__))

# Preset gain combinations. (pan_kp, pan_ki, pan_kd), (tilt...)
#
# Weighted towards D, because that is the term worth sweeping on this rig: the
# gimbal overshoots and rings, which D fixes and I does not. I is deliberately
# rare - it is only interesting as the fix for the steady-state offset the PD
# rows leave standing, and as the thing that winds up in Runaway. Tilt runs
# slightly softer than pan throughout because it works against gravity.
#
# Five groups, in grid order: one term at a time -> baselines -> the PD
# ladder (same P, rising D) -> PD at other P levels -> the PI presets.
PRESETS = [
    # --- one term at a time: what each does on its own ---
    {"name": "All Zero", "pan": (0.0, 0.0, 0.0), "tilt": (0.0, 0.0, 0.0)},
    {"name": "P-only", "pan": (50.0, 0.0, 0.0), "tilt": (45.0, 0.0, 0.0)},
    {"name": "I-only", "pan": (0.0, 6.0, 0.0), "tilt": (0.0, 6.0, 0.0)},
    {"name": "D-only", "pan": (0.0, 0.0, 6.0), "tilt": (0.0, 0.0, 5.0)},

    # --- baselines: the firmware default, and it plus D ---
    {"name": "Default (PI)", "pan": (40.0, 4.0, 0.0), "tilt": (35.0, 4.0, 0.0)},
    # Faster targeting: about 2x the default P, and a bigger I to close the
    # last bit of error sooner. Still under the Kp*k <= 0.5/T ceiling for
    # k = 0.02, T = 0.15 s (Kp <= 167). Made for the firmware slew clamps
    # (200 / 160 deg/s). Lower it if the dot starts hunting.
    {"name": "Recommended", "pan": (90.0, 12.0, 0.0), "tilt": (80.0, 10.0, 0.0)},
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

    # --- PI ladder: no D, I climbing at Default's P, then other P levels. Ki =
    # Kp^2 * k / 4 (~8 at Kp = 40) is critical damping; above it rings.
    {"name": "PI Light", "pan": (40.0, 2.0, 0.0), "tilt": (35.0, 2.0, 0.0)},
    {"name": "PI Heavy", "pan": (40.0, 8.0, 0.0), "tilt": (35.0, 7.0, 0.0)},
    {"name": "PI Sluggish", "pan": (25.0, 2.0, 0.0), "tilt": (22.0, 2.0, 0.0)},
    {"name": "Aggressive PI", "pan": (80.0, 10.0, 0.0), "tilt": (70.0, 9.0, 0.0)},
    {"name": "Soft PI", "pan": (10.0, 1.0, 0.0), "tilt": (8.0, 0.8, 0.0)},

    # Deliberately unstable: huge P with an integrator and nothing to damp it.
    # Keep it last - it is the "what does windup look like" demo, not a tuning
    # candidate.
    {"name": "Runaway", "pan": (500.0, 200.0, 0.0), "tilt": (500.0, 200.0, 0.0)},
]

_AXES = (("pan", "p"), ("tilt", "t"))
_TERMS = ("KP", "KI", "KD")

_OK = "#1a7f37"
_ERR = "#b42318"

_GAP = 8  # px between controls in a wrapping row


class Flow(ttk.Frame):
    """A row of widgets that wraps onto more lines when the window is narrow.

    Add children with `add()`; they are laid out left to right and start a new
    line when the next one would not fit. Re-run on every width change.
    """

    def __init__(self, parent):
        super().__init__(parent)
        self._items = []
        self._layout = None
        self.bind("<Configure>", self._on_configure)

    def add(self, widget):
        self._items.append(widget)
        self._layout = None
        return widget

    def _on_configure(self, event):
        x, row, col = 0, 0, 0
        places = []
        for w in self._items:
            need = w.winfo_reqwidth() + _GAP
            if col and x + need > event.width:
                row, col, x = row + 1, 0, 0
            places.append((row, col))
            col += 1
            x += need
        if places == self._layout:
            return
        self._layout = places
        for w, (r, c) in zip(self._items, places):
            w.grid(row=r, column=c, sticky="w", padx=(0, _GAP), pady=2)


def _wrap_to(label, parent):
    """Make a label's text wrap to the width of `parent`."""
    label.config(wraplength=300)   # until the first <Configure> says otherwise
    parent.bind("<Configure>",
                lambda e: label.config(wraplength=max(e.width - 12, 100)), add="+")


class Tooltip:
    """A small text window shown while the pointer is over `widget`."""

    def __init__(self, widget, text):
        self._widget, self._text, self._tip = widget, text, None
        widget.bind("<Enter>", self._show, add="+")
        widget.bind("<Leave>", self._hide, add="+")
        widget.bind("<ButtonPress>", self._hide, add="+")

    def _show(self, _event):
        if self._tip is not None:
            return
        tip = self._tip = tk.Toplevel(self._widget)
        tip.wm_overrideredirect(True)
        tip.wm_geometry(f"+{self._widget.winfo_rootx() + 12}"
                        f"+{self._widget.winfo_rooty() + self._widget.winfo_height() + 4}")
        tk.Label(tip, text=self._text, justify="left", wraplength=280,
                 background="#ffffe0", relief="solid", borderwidth=1,
                 padx=6, pady=4).pack()

    def _hide(self, _event=None):
        if self._tip is not None:
            self._tip.destroy()
            self._tip = None


def _titled_box(parent, title, tip):
    """A LabelFrame whose title is followed by an (i) icon showing `tip`."""
    head = ttk.Frame(parent)
    ttk.Label(head, text=title).pack(side="left")
    icon = ttk.Label(head, text="ⓘ", foreground="#1f6feb", cursor="question_arrow")
    icon.pack(side="left", padx=(4, 0))
    Tooltip(icon, tip)
    return ttk.LabelFrame(parent, labelwidget=head, padding=6)


def _pair(parent, text, var, width=7):
    """A caption followed by an entry, kept together when a row wraps."""
    f = ttk.Frame(parent)
    ttk.Label(f, text=text).pack(side="left")
    ttk.Entry(f, textvariable=var, width=width).pack(side="left", padx=(2, 0))
    return f


class Controls:
    """The left panel. Build it into `parent`; it shows its own status line."""

    def __init__(self, link, parent):
        self.link = link

        # Telemetry defaults ON: tracker.run() keeps re-sending 'T 1'
        # until a tlm sample lands (the firmware itself boots with it off, and
        # a single request can be lost to a reboot -- see the comment at its
        # call site), so the checkbox describes the board's actual state at
        # startup rather than contradicting it. Unticking it sends 'T 0' as
        # usual, which also clears link.telemetry_wanted so the retry loop
        # leaves it off instead of turning it back on.
        self.telemetry_on = tk.BooleanVar(value=True)
        # Not sent automatically: unlike telemetry, forcing the channel over
        # is a real behavioural change (it can take control away from
        # whatever else is driving the gimbal), so it waits for the operator
        # to press Set. AUTO is only the default shown here, since it is what
        # this script's own E frames need -- the firmware itself boots at NONE.
        self.channel = tk.StringVar(value="AUTO")
        # Mirrors the firmware default (boot.tour off); not read back from the
        # board. Ticking it sends the value, which the board saves in NVS.
        self.boot_tour = tk.BooleanVar(value=False)
        # PC-side only, nothing is sent: tracker.run() reads it every frame
        # (see recenter.py). On by default; unticking stops the drift at once.
        self.recenter_on = tk.BooleanVar(value=True)
        # One row per axis, one entry per term. Starts at the "Default (PI)"
        # preset, the firmware's own gains.
        default = next(p for p in PRESETS if p["name"] == "Default (PI)")
        self.gains = {axis: {t: tk.StringVar(value=f"{v:g}")
                             for t, v in zip(_TERMS, default[axis])}
                      for axis, _ in _AXES}
        self.preset_var = tk.StringVar(value=default["name"])
        # Set only by a successful Apply -- what the firmware actually got,
        # not whatever is sitting unsent in the table. error_graph.py reads
        # this for its snapshot filenames.
        self.applied_gains = None
        self.nudge = {k: tk.StringVar(value="5") for k in ("dpan", "dtilt")}
        # Starting point only, not read back from the board -- there is no
        # cfg.get sender. Values are firmware's compiled defaults
        # (Config.hpp's WORK_PAN_MIN/MAX, WORK_TILT_MIN/MAX).
        self.zone = {k: tk.StringVar(value=str(v)) for k, v in (
            ("pan_min", 45), ("pan_max", 105),
            ("tilt_min", 85), ("tilt_max", 115))}

        outer = ttk.Frame(parent, padding=8)
        outer.pack(fill="both", expand=True)
        self.frame = outer
        self._build_link_row(outer)
        self._build_gains(outer)
        self._build_zone(outer)
        self._build_nudge(outer)

        self.status = ttk.Label(outer, text="ready", foreground=_OK, justify="left")
        self.status.pack(fill="x", pady=(8, 0))
        _wrap_to(self.status, outer)

    # --- construction ------------------------------------------------------

    def add_actions(self, flow):
        """Put the one-shot action buttons into `flow`.

        They live with FIRE under the camera view (see app_window.py), not in
        this panel: they are things you press mid-run, while the fields here
        are things you set. Their results still land on this panel's status.
        """
        for text, command, tip in (
                ("Arm / Disarm (CONTROL)", self._press_control,
                 "Same as the board's CONTROL button: toggles ARMED / DISARMED, "
                 "or acknowledges a FAULT. Check 'st:' in the status."),
                ("Center", self._center,
                 "Moves to the middle of the current working zone (P frame -- "
                 "bypasses the PID and the selected channel)."),
                ("Start Zone Tour", self._zone_tour,
                 "Sweeps the working zone. Does nothing unless the gimbal is "
                 "DISARMED / PARKED."),
                ("Query gains", self._query,
                 "Asks the board for its gains. The reply is the 'esp32 |' "
                 "line in the console.")):
            Tooltip(flow.add(ttk.Button(flow, text=text, command=command)), tip)

    def _build_link_row(self, parent):
        row = Flow(parent)
        row.pack(fill="x")
        row.add(ttk.Checkbutton(row, text="Telemetry", variable=self.telemetry_on,
                                command=self._telemetry))
        tour = row.add(ttk.Checkbutton(row, text="Tour on boot", variable=self.boot_tour,
                                       command=self._boot_tour))
        Tooltip(tour, "Saved on the board. When on, the zone tour runs after "
                      "the self-test at every reset. Applies at the next reset.")
        recenter = row.add(ttk.Checkbutton(row, text="Recenter if laser lost",
                                           variable=self.recenter_on))
        Tooltip(recenter, "When the red dot is not seen for a while, move the "
                          "gimbal toward the zone centre until it is found "
                          "again. PC side only.")
        chan =row.add(ttk.Frame(row))
        ttk.Label(chan, text="Channel").pack(side="left", padx=(0, 4))
        ttk.Combobox(chan, textvariable=self.channel, width=8, state="readonly",
                     values=["NONE", "AUTO", "MANUAL"]).pack(side="left")
        ttk.Button(chan, text="Set", command=self._set_channel).pack(
            side="left", padx=(4, 0))

    def _build_gains(self, parent):
        box = _titled_box(parent, "PID gains", (
            "Pick a preset to fill the table (nothing is sent yet). Edit any "
            "value, then Apply sends pan and tilt gains together."))
        box.pack(fill="x", pady=(8, 0))

        # Picking a preset only fills the table; nothing is sent until Apply.
        top = ttk.Frame(box)
        top.pack(fill="x")
        ttk.Label(top, text="Preset").pack(side="left")
        combo = ttk.Combobox(top, textvariable=self.preset_var, state="readonly",
                             width=16, values=[p["name"] for p in PRESETS])
        combo.pack(side="left", padx=(4, 0), fill="x", expand=True)
        combo.bind("<<ComboboxSelected>>", lambda _e: self._fill_preset())

        table = ttk.Frame(box)
        table.pack(fill="x", pady=(6, 0))
        for col, term in enumerate(_TERMS, start=1):
            ttk.Label(table, text=term).grid(row=0, column=col)
            table.columnconfigure(col, weight=1)
        for row, (axis, _) in enumerate(_AXES, start=1):
            ttk.Label(table, text=axis).grid(row=row, column=0, sticky="w", padx=(0, 6))
            for col, term in enumerate(_TERMS, start=1):
                ttk.Entry(table, textvariable=self.gains[axis][term], width=7,
                          justify="right").grid(row=row, column=col, padx=2, pady=2,
                                                sticky="ew")
        ttk.Button(table, text="Apply", command=self._apply_gains).grid(
            row=1, column=len(_TERMS) + 1, rowspan=2, padx=(8, 0), sticky="ns")

    def _build_nudge(self, parent):
        box = _titled_box(parent, "Nudge gimbal (deg)", (
            "Applies a known physical kick -- like a bump, vibration, wind "
            "gust, or servo slip -- so the loop can correct it. Open loop."))
        box.pack(fill="x", pady=(8, 0))
        flow = Flow(box)
        flow.pack(fill="x")
        for name, var in self.nudge.items():
            flow.add(_pair(flow, name, var))
        flow.add(ttk.Button(flow, text="Nudge", command=self._do_nudge))

    def _build_zone(self, parent):
        box = _titled_box(parent, "Working zone (deg)", (
            "Sets the area the gimbal is allowed to move in, live. Set Max "
            "Zone opens it to the mechanical limits. Start Zone Tour and "
            "Center are under the view."))
        box.pack(fill="x", pady=(8, 0))

        # One row per axis, min and max columns; keys stay "<axis>_<min|max>".
        table = ttk.Frame(box)
        table.pack(fill="x")
        for col, text in enumerate(("min", "max"), start=1):
            ttk.Label(table, text=text).grid(row=0, column=col)
            table.columnconfigure(col, weight=1)
        for row, axis in enumerate(("pan", "tilt"), start=1):
            ttk.Label(table, text=axis).grid(row=row, column=0, sticky="w", padx=(0, 6))
            for col, edge in enumerate(("min", "max"), start=1):
                ttk.Entry(table, textvariable=self.zone[f"{axis}_{edge}"], width=7,
                          justify="right").grid(row=row, column=col, padx=2, pady=2,
                                                sticky="ew")
        buttons = ttk.Frame(table)
        buttons.grid(row=1, column=3, rowspan=2, padx=(8, 0), sticky="ns")
        ttk.Button(buttons, text="Set zone", command=self._set_zone).pack(fill="x")
        ttk.Button(buttons, text="Set Max Zone", command=self._set_max_zone).pack(
            fill="x", pady=(4, 0))

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

    def _set_channel(self):
        try:
            cid = self.link.set_channel(self.channel.get())
            self._say(f"channel -> {self.channel.get()} (cfg.set id {cid}); "
                      "confirmed by the overlay's 'ch:' field once a tlm sample lands")
        except Exception as exc:
            self._say(f"channel failed: {exc}", ok=False)

    def _press_control(self):
        try:
            cid = self.link.press_control()
            self._say(f"CONTROL pressed (cfg.set id {cid}); check 'st:' in the "
                      "overlay -- toggles ARMED/DISARMED, or acks a FAULT")
        except Exception as exc:
            self._say(f"CONTROL press failed: {exc}", ok=False)

    def _telemetry(self):
        on = self.telemetry_on.get()
        try:
            self.link.telemetry(1 if on else 0)
            self._say(f"telemetry {'ON' if on else 'OFF'}")
        except Exception as exc:
            # Put the checkbox back: it should show what the firmware was told.
            self.telemetry_on.set(not on)
            self._say(f"telemetry failed: {exc}", ok=False)

    def _apply_gains(self):
        try:
            values = {axis: self._floats(self.gains[axis]) for axis, _ in _AXES}
            for axis, code in _AXES:
                self.link.set_gains(code, *values[axis])
        except Exception as exc:
            self._say(f"set gains failed: {exc}", ok=False)
            return
        self.applied_gains = values
        self._say("gains applied: " + "; ".join(
            f"{axis} {', '.join(f'{v:g}' for v in values[axis])}"
            for axis, _ in _AXES))

    def gains_label(self):
        """Short filename-safe tag of the last applied gains, or None."""
        if self.applied_gains is None:
            return None
        return "_".join(
            f"{axis}-P{p:g}-I{i:g}-D{d:g}"
            for axis, _ in _AXES
            for p, i, d in [self.applied_gains[axis]])

    def _do_nudge(self):
        try:
            dpan, dtilt = self._floats(self.nudge)
            self.link.nudge(dpan, dtilt)
            self._say(f"nudge {dpan:g}, {dtilt:g}")
        except Exception as exc:
            self._say(f"nudge failed: {exc}", ok=False)

    def _set_zone(self):
        try:
            pan_min, pan_max, tilt_min, tilt_max = self._floats(self.zone)
            ids = self.link.set_zone(pan_min, pan_max, tilt_min, tilt_max)
            self._say(f"zone -> pan [{pan_min:g}, {pan_max:g}] "
                      f"tilt [{tilt_min:g}, {tilt_max:g}] (cfg.set ids {ids}); "
                      "check the 'esp32 |' cfg.state replies for rejections")
        except Exception as exc:
            self._say(f"set zone failed: {exc}", ok=False)

    def _set_max_zone(self):
        # Reads the firmware's own zone.limit.* keys rather than assuming a
        # copy of Config.hpp's constants on the PC side -- see
        # ErrorLink.read_zone_limits(). This blocks briefly (up to its
        # timeout) waiting for the four cfg.state replies.
        try:
            limits = self.link.read_zone_limits()
            missing = [k for k in ("pan_min", "pan_max", "tilt_min", "tilt_max")
                      if k not in limits]
            if missing:
                raise RuntimeError(f"no reply for {', '.join(missing)} "
                                   "-- is the board connected?")
        except Exception as exc:
            self._say(f"read max zone failed: {exc}", ok=False)
            return
        for key, value in limits.items():
            self.zone[key].set(f"{value:g}")
        self._set_zone()

    def _zone_tour(self):
        try:
            cid = self.link.zone_tour()
            self._say(f"zone tour requested (cfg.set id {cid}); watch 'st:' for "
                      "ZONE_TOUR -- no-ops unless the gimbal is DISARMED/PARKED")
        except Exception as exc:
            self._say(f"zone tour failed: {exc}", ok=False)

    def _boot_tour(self):
        try:
            on = self.boot_tour.get()
            cid = self.link.set_boot_tour(on)
            self._say(f"tour on boot -> {'on' if on else 'off'} (cfg.set id {cid}); "
                      "saved on the board, applies at the next reset")
        except Exception as exc:
            self._say(f"tour on boot failed: {exc}", ok=False)

    def _center(self):
        # Reads the current working zone live (see ErrorLink.center()) rather
        # than trusting these entry fields, which may not have been applied
        # yet -- blocks briefly waiting for the reply, same as Set Max Zone.
        try:
            pan, tilt = self.link.center()
            self._say(f"centering at pan={pan:g} tilt={tilt:g} "
                      "(P frame -- bypasses the PID and selected channel)")
        except Exception as exc:
            self._say(f"center failed: {exc}", ok=False)

    def _fill_preset(self):
        """Copy the chosen preset into the table. Sends nothing."""
        preset = next(p for p in PRESETS if p["name"] == self.preset_var.get())
        for axis, _ in _AXES:
            for term, value in zip(_TERMS, preset[axis]):
                self.gains[axis][term].set(f"{value:g}")
        self._say(f"preset {preset['name']} filled - press Apply to send")
