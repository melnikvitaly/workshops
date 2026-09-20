"""Drift the gimbal toward the working-zone centre while the laser dot is lost.

The gimbal can point the laser outside the camera view. The red dot is then
not found, the error is valid=0 and the firmware only holds, so nothing brings
the laser back. This module decides when to start moving and where the next
step goes; tracker.py sends it as a `P` frame (ErrorLink.move_to).

Pure logic (no serial, no Tk), so it is easy to test:

    active = recenter.update(red_seen, now)
    if active:
        if not recenter.started:
            recenter.begin(pos, centre)
        waypoint = recenter.step(now)                # (pan, tilt) or None

`P` is an instant servo move that cannot be cancelled, so the motion is a
series of small waypoints. It stops the moment the red dot is seen again
(update() returns False, no more waypoints), or when the centre is reached.
"""

_MIN_STEP_S = 0.02   # at most 50 waypoints/s, whatever the frame rate
_MAX_STEP_S = 0.1    # a stalled loop must not turn into one big jump
_ARRIVED_DEG = 0.5   # this close to the centre counts as there


class LaserLostRecenter:
    def __init__(self, timeout_ms=1500.0, speed_deg_s=30.0):
        self.timeout_s = timeout_ms / 1000.0
        self.speed = speed_deg_s
        self._lost_since = None   # time the dot was first missing, else None
        self._pos = None          # our own waypoint, so a lagging tlm cannot slow us
        self._centre = None
        self._last_step = None

    def reset(self):
        self._lost_since = None
        self._pos = None
        self._centre = None
        self._last_step = None

    @property
    def started(self):
        return self._pos is not None

    def begin(self, pos, centre):
        """Start moving from `pos` (pan, tilt) toward `centre`. Call once per
        lost event, when `started` is False."""
        self._pos = tuple(pos)
        self._centre = tuple(centre)
        self._last_step = None

    def update(self, red_seen, now):
        """Feed one frame. True while recentering is due (dot lost for at
        least `timeout_s`). A seen dot ends it at once."""
        if red_seen or self.timeout_s <= 0.0:
            self.reset()
            return False
        if self._lost_since is None:
            self._lost_since = now
        return now - self._lost_since >= self.timeout_s

    def step(self, now):
        """Next waypoint `(pan, tilt)` toward the centre, or None (too soon
        after the last one, or already at the centre)."""
        if self._last_step is None:
            dt = _MIN_STEP_S
        else:
            dt = now - self._last_step
            if dt < _MIN_STEP_S:
                return None
            dt = min(dt, _MAX_STEP_S)
        self._last_step = now

        if all(abs(g - c) <= _ARRIVED_DEG for g, c in zip(self._centre, self._pos)):
            return None
        reach = self.speed * dt
        nxt = []
        for cur, goal in zip(self._pos, self._centre):
            delta = goal - cur
            nxt.append(goal if abs(delta) <= reach
                       else cur + reach * (1.0 if delta > 0 else -1.0))
        self._pos = tuple(nxt)
        return self._pos
