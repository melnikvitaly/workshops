"""Keyboard-driven MANUAL channel: arrow keys move the gimbal by direct velocity.

The firmware's MANUAL input channel (firmware/aim/src/parts/ManualChannel.hpp)
drives the gimbal straight off the last `M <vpan> <vtilt>` frame and fails
safe the same way AUTO does on `E` frames -- config::TRACK_TIMEOUT_MS
(300 ms) without a fresh one parks the gimbal. So driving from the keyboard
still means refreshing the last frame periodically while a direction key is
held, even if the commanded velocity has not changed.

"Held" is read from the actual OS key state (`_held()`, `GetAsyncKeyState`),
not from key-repeat events: Tk delivers key-down repeats but no key-up, and
guessing a release from a missing repeat misread real holds. Reading the OS
state means "held" means held, with no timing guesswork.

Windows only (this rig runs on Windows) -- elsewhere `_held()`/`_focused()`
always report false, so engaging keyboard drive is a documented no-op there
rather than a silent one.

GetAsyncKeyState reads the *global* keyboard state, not per-window input, so
`_focused()` gates it on the main window being the foreground window, and the
optional `active` callback on the caller saying no text field has the keyboard
-- without those, holding an arrow key would drive the gimbal while some other
application (or a gain entry box) has focus.

Arrow keys only, not WASD: 'd' is the debug toggle in tracker.py, and every
key arrives in the same stream this module's caller reads (see
simulated_target.py's docstring for the same collision, on the same keys).
`handle_key()` still consumes the key event for those four keys, so
simulated_target.py's own arrow-key nudge does not also fire while keyboard
drive is engaged; the actual commanded velocity never comes from the event.

Engaging keyboard drive (`toggle()`, bound to 'm' in tracker.py) also
switches the firmware to MANUAL (ErrorLink.set_channel) -- the same explicit
action as picking MANUAL + Set in the left panel, justified here because
pressing 'm' is itself the operator's explicit request to drive by keyboard.
Disengaging sends one immediate zero-velocity frame rather than waiting out
the 300 ms failsafe, then stops sending; it does not change the channel back,
since the operator may still want MANUAL selected.

Keyboard drive also ends by itself, without sending anything, when the channel
leaves MANUAL: the left panel picked another one (`link.channel_requested`,
seen in `tick()`), or a `tlm` sample reports another `ch` after MANUAL was
confirmed (`note_channel()`, e.g. the MODE button). The firmware would drop
every `M` frame on a non-selected channel anyway.
"""

import ctypes
import math
import sys
import time

# Windows virtual-key codes for the arrow keys -> (dpan, dtilt) in
# gimbal-velocity space, one unit per key. What `_held()` actually polls.
# Pan is inverted relative to the arrow's on-screen direction -- left/right on
# this rig's gimbal runs opposite the arrow-key/on-screen sense, not opposite
# the wire's vpan sign -- so VK_LEFT/VK_RIGHT are swapped here to match.
_VK_ARROWS = {0x25: (1.0, 0.0), 0x26: (0.0, -1.0), 0x27: (-1.0, 0.0), 0x28: (0.0, 1.0)}

# Tk keysyms for the same four keys. Used only by handle_key() to swallow the
# event (see the module docstring); not used to compute velocity.
_KEYS = {"Left", "Up", "Right", "Down"}

_WIN_TITLE = "gimbal eye"   # app_window.TITLE -- see _focused()

# Refresh comfortably inside config::TRACK_TIMEOUT_MS (300 ms) even when the
# commanded velocity has not changed, so a held key does not itself trip the
# firmware's own link-timeout failsafe.
_KEEPALIVE_S = 0.15


def _held(vk):
    """True if virtual-key `vk` is down right now, per the OS."""
    if sys.platform != "win32":
        return False
    return bool(ctypes.windll.user32.GetAsyncKeyState(vk) & 0x8000)


def _focused():
    """True if the main window is the foreground window."""
    if sys.platform != "win32":
        return False
    user32 = ctypes.windll.user32
    hwnd = user32.FindWindowW(None, _WIN_TITLE)
    return hwnd != 0 and hwnd == user32.GetForegroundWindow()


class ManualControl:
    """Arrow keys -> `M <vpan> <vtilt>` frames while keyboard drive is engaged.

    Usage (see tracker.py): construct once, then each loop iteration call
    `handle_key(raw_key)` for the key just read (only consumes arrow keys, and
    only while engaged) and `tick()` unconditionally, whether or not a key
    arrived -- `tick()` decides for itself whether anything needs to be sent.
    """

    def __init__(self, link, speed_deg_s=40.0, active=None):
        self.link = link
        self._active = active or (lambda: True)
        self.speed = speed_deg_s
        self.engaged = False
        self._last_sent = None    # (vpan, vtilt) most recently written to the wire
        self._last_sent_at = 0.0
        self._seen_manual = False  # a tlm sample has reported ch:MANUAL since engaging

    def toggle(self):
        """Flip keyboard drive on/off. Returns the new state."""
        self.engaged = not self.engaged
        if self.engaged:
            self._seen_manual = False
            self.link.set_channel("MANUAL")
        else:
            self._last_sent = None
            self.link.manual_now(0.0, 0.0)
        return self.engaged

    def note_channel(self, channel):
        """Feed the `ch` field of each tlm sample. Once the firmware has
        reported MANUAL, any other value (left panel, MODE button)
        means the operator left MANUAL: stop sending. Before MANUAL is first
        seen a stale sample still shows the old channel, so it is ignored."""
        if not self.engaged:
            return
        if channel == "MANUAL":
            self._seen_manual = True
        elif self._seen_manual:
            self._release()

    def _release(self):
        """Leave keyboard drive without sending anything: the channel is no
        longer MANUAL, so the firmware would drop every `M` frame anyway."""
        self.engaged = False
        self._last_sent = None
        print("keyboard MANUAL drive off -- channel is no longer MANUAL")

    def toggle_pressed(self, key):
        """True if `key` (a Tk keysym) is the 'm' toggle -- the caller then
        toggle()s. Shift/CapsLock turn it into 'M', so both count."""
        return key in ("m", "M")

    def handle_key(self, key):
        """True if `key` (a Tk keysym) was an arrow direction
        AND keyboard drive is engaged -- the caller should treat it as
        consumed (e.g. not also hand it to simulated_target's arrow nudge).
        Purely an event-swallow; see the module docstring for why this does
        not feed into the commanded velocity."""
        return self.engaged and key in _KEYS

    def velocity(self):
        """Current commanded (vpan, vtilt), deg/s, from the arrow keys
        actually down right now. Two opposite keys cancel; two adjacent ones
        (e.g. up+right) are normalised so a diagonal is not faster than one
        axis alone."""
        if not self.engaged or not _focused() or not self._active():
            return 0.0, 0.0
        dpan = dtilt = 0.0
        for vk, (dp, dt) in _VK_ARROWS.items():
            if _held(vk):
                dpan += dp
                dtilt += dt
        if dpan == 0.0 and dtilt == 0.0:
            return 0.0, 0.0
        norm = max(1.0, math.hypot(dpan, dtilt))
        return dpan / norm * self.speed, dtilt / norm * self.speed

    def tick(self):
        """Send the current velocity, but only when it actually changed or
        the last frame is old enough that the firmware's 300 ms failsafe
        needs a keep-alive (see `_KEEPALIVE_S`) -- not on every call.

        The keep-alive runs even at zero velocity, and that is required, not
        just belt-and-braces: while MANUAL is the selected channel, the
        firmware drops every `E` frame as drop_inactive before it reaches the
        controller (LinkUart.hpp), so these `M` frames are the *only* thing
        keeping `_lastFrameMs`/`linkFresh` alive (Ctrl.hpp). Safety.hpp's
        laser interlock is gated on that same `linkFresh` while ARMED -- let
        it go stale, even while sitting at a correctly-commanded zero, and
        the state machine drops out of Armed and the beam goes with it. Only
        `toggle()` disengaging is allowed to let the link actually go stale.

        No-op while disengaged -- toggle() already sent the zero frame that
        stops the gimbal, and the 300 ms failsafe (correctly, this time)
        takes the state machine and the laser down with it."""
        if not self.engaged:
            return
        requested = getattr(self.link, "channel_requested", "MANUAL")
        if requested != "MANUAL":
            self._release()
            return
        vpan, vtilt = self.velocity()
        now = time.time()
        changed = self._last_sent is None or (vpan, vtilt) != self._last_sent
        stale = now - self._last_sent_at >= _KEEPALIVE_S
        if not (changed or stale):
            return
        if self.link.manual_now(vpan, vtilt):
            self._last_sent = (vpan, vtilt)
            self._last_sent_at = now
