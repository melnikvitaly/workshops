"""The on-screen FIRE button drawn over the camera view."""

import time

import cv2

from overlay import _RED, _GREEN, _WHITE, _ui_scale


class FireButton:
    """An on-screen FIRE button: click it (or press 'f') to send one 'F'.

    HighGUI has no widgets unless OpenCV was built with Qt, so the button is
    just a rectangle painted on the frame plus a mouse callback that tests
    whether a click landed inside it. It is drawn AFTER any --rotate, so what
    the callback receives in window coordinates is what the user aimed at.

    Its border carries the loop's state, so the operator can watch one thing
    instead of reading numbers:

        green   -- still converging, the error is too big to shoot on
        red     -- the error is small: on target
        blinking-- the ESP32 just reported ARRIVAL (its own axes settled)
    """

    BLINKS = 4
    BLINK_PERIOD = 0.16     # seconds per half-cycle

    def __init__(self):
        self._rect = (0, 0, 0, 0)
        self._clicked = False
        self._lit_until = 0.0
        self._blink_until = 0.0

    def blink(self):
        """Flash the border - the firmware says it has arrived on target."""
        self._blink_until = time.time() + 2 * self.BLINKS * self.BLINK_PERIOD

    def _border(self, now, on_target):
        """(colour, extra thickness) of the border right now."""
        if now < self._blink_until:
            # Alternate on the half-cycle. Counting down from the end keeps the
            # phase independent of the frame rate.
            phase = int((self._blink_until - now) / self.BLINK_PERIOD) % 2
            return ((255, 255, 255) if phase else (0, 215, 255)), 2
        return (_RED if on_target else _GREEN), 0

    def draw(self, view, on_target=False):
        h, w = view.shape[:2]
        s = _ui_scale(view)
        bw, bh, margin = int(150 * s), int(52 * s), int(14 * s)
        x1, y1 = margin, h - margin - bh
        x2, y2 = x1 + bw, y1 + bh
        self._rect = (x1, y1, x2, y2)

        now = time.time()
        lit = now < self._lit_until           # briefly after a shot is sent
        colour, extra = self._border(now, on_target)

        fill = (40, 40, 220) if lit else (30, 30, 90)
        cv2.rectangle(view, (x1, y1), (x2, y2), fill, -1)
        cv2.rectangle(view, (x1, y1), (x2, y2), colour,
                      max(1, int(2 * s)) + extra)
        cv2.putText(view, "FIRE (f)", (x1 + int(16 * s), y1 + int(34 * s)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7 * s, _WHITE,
                    max(1, int(2 * s)), cv2.LINE_AA)
        return view

    def contains(self, x, y):
        """True if (x, y) in window coordinates lands on the button."""
        x1, y1, x2, y2 = self._rect
        return x1 <= x <= x2 and y1 <= y <= y2

    def on_mouse(self, event, x, y, flags, _param):
        if event != cv2.EVENT_LBUTTONDOWN:
            return
        if self.contains(x, y):
            self._clicked = True

    def take(self):
        """True once per click/keypress; also lights the button for 300 ms."""
        if not self._clicked:
            return False
        self._clicked = False
        self.trigger()
        return True

    def trigger(self):
        self._lit_until = time.time() + 0.3
