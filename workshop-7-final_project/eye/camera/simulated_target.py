"""Simulated target handling extracted from tracker.py.

Provides SimulatedTargetManager to manage a user-created simulated target dot
via mouse interaction. It maps display coords (the view, after any --rotate)
back to frame coords and creates/removes a `Dot` when appropriate.

Left-click places or moves the dot, right-click clears it, and the arrow keys
nudge it (starting at the frame centre if there is none yet).
"""
import math
from typing import Optional

from dots import Dot

# Arrow keys only -> (dx, dy) in display coordinates, one `step` per press.
#
# Letters used to move the dot too (WASD), but every key arrives in the same
# stream: 'd' meant both "move right" and "toggle the debug view", so the
# toggle was unreachable. Arrows collide with nothing. The names are Tk keysyms.
_MOVE = {"Left": (-1, 0), "Up": (0, -1), "Right": (1, 0), "Down": (0, 1)}


class SimulatedTargetManager:
    def __init__(self, rotate=0):
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
        if key not in _MOVE:
            return False
        if self.simulated_target is None:
            if self.last_frame_shape is None:
                return False
            h, w = self.last_frame_shape[:2]
            x = w / 2
            y = h / 2
            self._create_at(x, y)
        fx, fy = self.simulated_target.center
        dx, dy = _MOVE[key]
        display_x, display_y = self._frame_to_display(fx, fy)
        new_display_x = display_x + dx * self.step
        new_display_y = display_y + dy * self.step
        new_frame_x, new_frame_y = self._map_display_to_frame(new_display_x, new_display_y)
        new_frame_x = max(0, min(new_frame_x, self.last_frame_shape[1] - 1))
        new_frame_y = max(0, min(new_frame_y, self.last_frame_shape[0] - 1))
        self._create_at(new_frame_x, new_frame_y)
        return True

    def clear(self):
        """Right-click: drop the simulated target."""
        self.simulated_target = None

    def place_display(self, x, y):
        """Left-click at display coords (x, y): place or move the simulated dot."""
        # Map clicked display coords back to original frame coords
        fx, fy = self._map_display_to_frame(x, y)
        # If an actual target dot is detected near the click, don't simulate.
        for t in self.targets_current:
            dx = t.center[0] - fx
            dy = t.center[1] - fy
            if math.hypot(dx, dy) <= max(12, t.radius):
                return
        self._create_at(fx, fy)
