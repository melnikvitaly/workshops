"""Simulated target handling extracted from detect_dots.py.

Provides SimulatedTargetManager to manage a user-created simulated black dot
via mouse interaction. It forwards clicks to the `FireButton`, maps display
coords back to frame coords considering rotation, and creates/removes a
`Dot` when appropriate.

Left-click places or moves the dot, right-click clears it, and the arrow keys
nudge it (starting at the frame centre if there is none yet).
"""
import math
from typing import Optional

from dots import Dot
import cv2

# Arrow keys only -> (dx, dy) in display coordinates, one `step` per press.
#
# Letters used to move the dot too (WASD), but every HighGUI key arrives in the
# same stream: 'd' meant both "move right" and "toggle the mask windows", and
# handle_key runs first, so the mask toggle was unreachable. Arrows collide with
# nothing.
#
# Which code an arrow arrives as depends on the OpenCV backend, so all the known
# ones are listed: Windows extended (0x25-0x28 << 16), X11/GTK keysyms, and the
# 574xx set some Qt builds report. The bare scan codes 72/75/77/80 and 81-84 are
# deliberately NOT here - they are indistinguishable from ASCII 'H','K','M','P'
# and 'Q','R','S','T', which is how the collision got in.
_MOVE = {
    2424832: (-1, 0), 2490368: (0, -1), 2555904: (1, 0), 2621440: (0, 1),
    65361: (-1, 0), 65362: (0, -1), 65363: (1, 0), 65364: (0, 1),
    57448: (-1, 0), 57449: (0, -1), 57450: (1, 0), 57451: (0, 1),
}


class SimulatedTargetManager:
    def __init__(self, fire, rotate=0):
        self.fire = fire
        self.rotate = rotate
        self.step = 24
        self.simulated_target: Optional[Dot] = None
        self.targets_current = []
        self.last_frame_shape = None

    def update_state(self, targets, frame_shape):
        self.targets_current = targets
        self.last_frame_shape = frame_shape

    def get_simulated(self):
        return self.simulated_target

    def _map_display_to_frame(self, x, y):
        fs = self.last_frame_shape
        if fs is None:
            return x, y
        h, w = fs[:2]
        if self.rotate == 0:
            return x, y
        if self.rotate == 90:
            return (w - 1 - y, x)
        if self.rotate == 180:
            return (w - 1 - x, h - 1 - y)
        if self.rotate == 270:
            return (y, h - 1 - x)
        return x, y

    def _frame_to_display(self, x, y):
        fs = self.last_frame_shape
        if fs is None:
            return x, y
        h, w = fs[:2]
        if self.rotate == 0:
            return x, y
        if self.rotate == 90:
            return (y, w - 1 - x)
        if self.rotate == 180:
            return (w - 1 - x, h - 1 - y)
        if self.rotate == 270:
            return (h - 1 - y, x)
        return x, y

    def _create_at(self, x, y):
        r = 12
        area = math.pi * (r ** 2)
        x1 = max(0, int(round(x - r)))
        y1 = max(0, int(round(y - r)))
        x2 = int(round(x + r))
        y2 = int(round(y + r))
        self.simulated_target = Dot(float(x), float(y), area, 1.0, (x1, y1, x2, y2))

    def handle_key(self, key):
        if int(key) not in _MOVE:
            return False
        if self.simulated_target is None:
            if self.last_frame_shape is None:
                return False
            h, w = self.last_frame_shape[:2]
            x = w / 2
            y = h / 2
            self._create_at(x, y)
        fx, fy = self.simulated_target.center
        dx, dy = _MOVE[int(key)]
        display_x, display_y = self._frame_to_display(fx, fy)
        new_display_x = display_x + dx * self.step
        new_display_y = display_y + dy * self.step
        new_frame_x, new_frame_y = self._map_display_to_frame(new_display_x, new_display_y)
        new_frame_x = max(0, min(new_frame_x, self.last_frame_shape[1] - 1))
        new_frame_y = max(0, min(new_frame_y, self.last_frame_shape[0] - 1))
        self._create_at(new_frame_x, new_frame_y)
        return True

    def on_mouse(self, event, x, y, flags, param):
        # Let the FIRE button inspect the event first (it ignores right clicks).
        try:
            self.fire.on_mouse(event, x, y, flags, param)
        except Exception:
            pass
        # If click was inside FIRE button, skip simulated-target handling.
        if self.fire.contains(x, y):
            return
        # Right-click removes any simulated target.
        if event == cv2.EVENT_RBUTTONDOWN:
            self.simulated_target = None
            return
        if event != cv2.EVENT_LBUTTONDOWN:
            return
        # Map clicked display coords back to original frame coords
        fx, fy = self._map_display_to_frame(x, y)
        # If an actual black dot is detected near the click, don't simulate.
        for t in self.targets_current:
            dx = t.center[0] - fx
            dy = t.center[1] - fy
            if math.hypot(dx, dy) <= max(12, t.radius):
                return
        # Create or move a simulated Dot at that location.
        r = 12
        area = math.pi * (r ** 2)
        x1 = max(0, int(round(fx - r)))
        y1 = max(0, int(round(fy - r)))
        x2 = int(round(fx + r))
        y2 = int(round(fy + r))
        self.simulated_target = Dot(float(fx), float(fy), area, 1.0, (x1, y1, x2, y2))
