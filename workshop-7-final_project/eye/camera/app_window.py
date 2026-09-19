"""The one window: controls on the left, camera view in the middle, tuning on the right.

    +----------+---------------------------+-----------+
    | controls |        camera view        |  debug    |
    | (gains,  |                           |  masks    |
    |  nudge,  |                           |  sliders  |
    |  zone…)  +---------------------------+  error    |
    |  status  |  FIRE   keyboard drive    |  graph    |
    +----------+---------------------------+-----------+

Everything is Tk. The camera view is a canvas showing the annotated frame as a
PPM image (Tk reads PPM natively, so no Pillow), scaled to fit and centred.
Clicks on it are mapped back to image pixels.

There is no `mainloop`: `pump()` is called once per frame from the detect_dots
loop, which keeps every callback on the main thread. There is also no HighGUI
window any more, so nothing calls `cv2.waitKey`; keys arrive as Tk events and
are queued for the loop to read with `next_key()`.
"""

import collections
import os
import sys
import time
import tkinter as tk
from tkinter import ttk

import cv2

if __package__ in (None, ""):
    sys.path.insert(0, os.path.dirname(__file__))

from controls import Controls, Flow
from error_graph import ErrorGraphWindow

TITLE = "gimbal eye"   # manual_control._WIN_TITLE finds the window by this

# Widgets that take typed text: keys pressed there belong to them, not to the
# view ('f' typed into a gain box must not fire the laser).
_TEXT_CLASSES = {"Entry", "TEntry", "TCombobox", "Text", "Spinbox", "TSpinbox"}

_LEFT_W = 360
_RIGHT_W = 400
_STRIP_W = 30   # a folded panel keeps just its button

_GREEN = "#1a7f37"
_RED = "#c62828"


def _photo(bgr, max_w, max_h):
    """(PhotoImage, scale) of a BGR image shrunk or grown to fit max_w x max_h."""
    h, w = bgr.shape[:2]
    scale = min(max_w / w, max_h / h)
    nw, nh = max(1, int(w * scale)), max(1, int(h * scale))
    if (nw, nh) != (w, h):
        bgr = cv2.resize(bgr, (nw, nh),
                         interpolation=cv2.INTER_AREA if scale < 1 else cv2.INTER_LINEAR)
    ppm = cv2.imencode(".ppm", bgr)[1].tobytes()
    return tk.PhotoImage(data=ppm, format="PPM"), scale


class AppWindow:
    """Build it, then call `pump()` once per frame and `show_frame()` per view."""

    def __init__(self, link, thresholds, speed, debug=False):
        self.alive = True
        self._keys = collections.deque()
        self._fire_clicked = False
        self._on_target = False

        self.root = tk.Tk()
        self.root.title(TITLE)
        self.root.geometry("1500x860+40+40")
        self.root.minsize(900, 500)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.bind_all("<Key>", self._on_key)
        # Picking from a dropdown leaves it holding the keyboard; hand it back
        # to the view so 'f', 'q' and the arrows work again.
        self.root.bind_class("TCombobox", "<<ComboboxSelected>>",
                             lambda _e: self.canvas.focus_set(), add="+")

        panes = ttk.PanedWindow(self.root, orient="horizontal")
        panes.pack(fill="both", expand=True)
        # Side panels get a fixed starting width. Without propagate(False) a
        # pane asks for whatever its widest child wants (an unwrapped help
        # sentence is well over 1000 px) and squeezes the view out.
        self._panes = panes
        left, left_body, _ = self._side_panel(panes, 0, _LEFT_W, "left")
        centre = ttk.Frame(panes)
        right, right_body, fold_right = self._side_panel(panes, 1, _RIGHT_W, "right")
        panes.add(left, weight=0)
        panes.add(centre, weight=1)
        panes.add(right, weight=0)

        # Packed first, on the bottom edge, so Controls fills what is left.
        self._build_status(left_body)
        self.controls = Controls(link, left_body)
        self._build_view(centre)
        self._build_right(right_body, thresholds, speed, debug)
        # The tuning panel starts folded: it is for bring-up, not for driving.
        # The sash can only be placed once the panes have a real size.
        self.root.update()
        fold_right()

    def _side_panel(self, panes, sash, width, edge):
        """A pane with a body and a slim strip whose button folds the body away.

        `edge` is the side the pane sits on. The strip goes on the edge facing
        the view, and its arrow points where the panel will go. Returns the
        pane, its body, and the function that flips it.
        """
        pane = ttk.Frame(panes, width=width)
        pane.pack_propagate(False)
        strip = ttk.Button(pane, width=2)
        strip.pack(side="right" if edge == "left" else "left", fill="y")
        body = ttk.Frame(pane)
        body.pack(side=edge, fill="both", expand=True)
        state = {"open": True, "width": width}

        def arrows():
            fold = "◀" if edge == "left" else "▶"
            strip.config(text=fold if state["open"] else
                         ("▶" if edge == "left" else "◀"))

        def toggle():
            if state["open"]:
                state["width"] = max(pane.winfo_width(), _STRIP_W * 2)
                body.pack_forget()
                target = _STRIP_W
            else:
                body.pack(side=edge, fill="both", expand=True)
                target = state["width"]
            state["open"] = not state["open"]
            arrows()
            self.root.update_idletasks()
            total = panes.winfo_width()
            panes.sashpos(sash, target if edge == "left" else total - target)

        strip.config(command=toggle)
        arrows()
        return pane, body, toggle

    # --- construction ------------------------------------------------------

    def _build_status(self, parent):
        box = ttk.LabelFrame(parent, text="Status", padding=6)
        box.pack(side="bottom", fill="x", padx=8, pady=(0, 8))
        self._status = ttk.Label(box, font=("Consolas", 11), justify="left",
                                 wraplength=_LEFT_W - 60)
        self._status.pack(fill="x")

    def _build_view(self, parent):
        bar = ttk.Frame(parent, padding=(8, 6))
        bar.pack(side="bottom", fill="x")
        hint = ttk.Label(bar, text="click = place target · right-click = clear · "
                                   "arrows = move it")
        hint.pack(side="bottom", anchor="w")
        # One wrapping row: FIRE first, then everything else you press mid-run.
        actions = Flow(bar)
        actions.pack(side="top", fill="x")
        self.fire_btn = actions.add(tk.Button(
            actions, text="FIRE (f)", font=("Segoe UI", 14, "bold"), fg="white",
            bg=_GREEN, activebackground=_RED, activeforeground="white",
            width=12, command=self._fire_pressed))
        self.drive_btn = actions.add(ttk.Button(
            actions, text="Keyboard drive: off (m)",
            command=lambda: self._keys.append("m")))
        self.controls.add_actions(actions)

        self.canvas = tk.Canvas(parent, bg="#111111", highlightthickness=0,
                                width=320, height=240, takefocus=True)
        self.canvas.pack(side="top", fill="both", expand=True)
        self._image_item = self.canvas.create_image(0, 0, anchor="nw")
        self._photo = None
        self._map = (0, 0, 1.0, 1, 1)          # x offset, y offset, scale, w, h
        self.on_click = lambda x, y: None      # image coordinates, left button
        self.on_clear = lambda: None           # right button
        self.canvas.bind("<Button-1>", self._on_left)
        self.canvas.bind("<Button-3>", self._on_right)
        self.canvas.focus_set()

    def _build_right(self, parent, thresholds, speed, debug):
        top = ttk.Frame(parent, padding=(8, 6))
        top.pack(fill="x")
        self.debug = tk.BooleanVar(value=debug)
        ttk.Checkbutton(top, text="Debug view (d)", variable=self.debug,
                        command=self._debug_changed).pack(side="left")
        ttk.Button(top, text="Print flags (p)",
                   command=lambda: self._keys.append("p")).pack(side="right")

        # The mask image sits above the sliders, so a slider moves under the
        # eye that is watching the mask react.
        self._mask_label = ttk.Label(parent)
        self._mask_photo = None
        if debug:
            self._mask_label.pack(fill="x", padx=8)

        speed.build(parent).pack(fill="x", padx=8, pady=(6, 0))

        self.sliders = thresholds.build(parent)
        self.sliders.pack(fill="x", padx=8, pady=(6, 0))

        self.error_graph = ErrorGraphWindow(parent)
        self.error_graph.frame.pack(fill="both", expand=True, padx=8, pady=8)
        # Let the plot shrink to the panel instead of forcing it wide.
        self.error_graph.canvas.get_tk_widget().config(width=200, height=160)

    # --- events ------------------------------------------------------------

    def _on_close(self):
        self.alive = False

    def _on_key(self, event):
        if event.widget.winfo_class() in _TEXT_CLASSES:
            return
        self._keys.append(event.keysym)

    def _fire_pressed(self):
        self._fire_clicked = True
        self.canvas.focus_set()

    def toggle_debug(self):
        self.debug.set(not self.debug.get())
        self._debug_changed()

    def _debug_changed(self):
        self.canvas.focus_set()
        if not self.debug.get():
            self._mask_label.pack_forget()
            self._mask_photo = None
        else:
            self._mask_label.pack(fill="x", padx=8, before=self.sliders)

    def _to_image(self, event):
        """Canvas click -> image pixel, or None if it landed on the letterbox."""
        ox, oy, scale, w, h = self._map
        x, y = (event.x - ox) / scale, (event.y - oy) / scale
        return (x, y) if 0 <= x < w and 0 <= y < h else None

    def _on_left(self, event):
        self.canvas.focus_set()
        pt = self._to_image(event)
        if pt is not None:
            self.on_click(*pt)

    def _on_right(self, event):
        self.canvas.focus_set()
        self.on_clear()

    # --- what the loop uses --------------------------------------------------

    def keys_ready(self):
        """True if the keyboard belongs to the view (no text box has focus)."""
        try:
            w = self.root.focus_get()
        except (KeyError, tk.TclError):
            return True
        return w is None or w.winfo_class() not in _TEXT_CLASSES

    def next_key(self):
        """The oldest unread Tk keysym ('f', 'Left', 'space', ...), or None."""
        return self._keys.popleft() if self._keys else None

    def take_fire(self):
        """True once per click on the FIRE button."""
        clicked, self._fire_clicked = self._fire_clicked, False
        return clicked

    def flash_fire(self):
        """Light the button briefly - a shot was sent (button or key)."""
        self.fire_btn.config(relief="sunken")
        self.root.after(300, lambda: self.fire_btn.config(relief="raised"))

    def set_on_target(self, on):
        """Red when the error is small enough to shoot on, green while converging."""
        if on != self._on_target:
            self._on_target = on
            self.fire_btn.config(bg=_RED if on else _GREEN)

    def set_drive(self, engaged):
        self.drive_btn.config(text=f"Keyboard drive: {'ON' if engaged else 'off'} (m)")

    def record_error(self, dx, dy):
        self.error_graph.append(dx, dy)

    def show_frame(self, view):
        """Draw a BGR image into the view, scaled to fit."""
        cw, ch = max(self.canvas.winfo_width(), 2), max(self.canvas.winfo_height(), 2)
        self._photo, scale = _photo(view, cw, ch)
        nw, nh = self._photo.width(), self._photo.height()
        ox, oy = (cw - nw) // 2, (ch - nh) // 2
        self._map = (ox, oy, scale, view.shape[1], view.shape[0])
        self.canvas.coords(self._image_item, ox, oy)
        self.canvas.itemconfig(self._image_item, image=self._photo)

    def set_status(self, lines):
        """Show the per-frame status text (see overlay.status_lines)."""
        self._status.config(text="\n".join(lines))

    def show_masks(self, image):
        """Draw the debug mask image (BGR) above the sliders."""
        width = max(self._mask_label.winfo_width(), _RIGHT_W - 24)
        self._mask_photo, _ = _photo(image, width, 260)
        self._mask_label.config(image=self._mask_photo)

    def pump(self):
        """Service Tk's event queue. False once the window has been closed.

        `update`, not `mainloop`: the detect_dots loop owns the thread and this
        borrows it for as long as the queued callbacks take.
        """
        if not self.alive:
            return False
        try:
            self.root.update()
        except tk.TclError:
            self.alive = False
        return self.alive

    def wait_key(self, block):
        """One key (Tk keysym) or None. With `block`, wait for one.

        Folder mode wants one image per press, so it waits here - but in short
        slices, so the window keeps painting and answering clicks. Returns
        "q" if the window was closed meanwhile.
        """
        while True:
            if not self.pump():
                return "q"
            key = self.next_key()
            if key is not None or not block:
                return key
            time.sleep(0.015)

    def close(self):
        self.alive = False
        try:
            self.error_graph.close()
            self.root.destroy()
        except tk.TclError:
            pass
