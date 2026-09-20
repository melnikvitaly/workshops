"""Frame drawing for tracker: detections, error vector, and the mask image.

Everything here is display-only. Detection coordinates are always in original
camera space, so the overlay is drawn BEFORE any --rotate is applied to the
view (see _ROTATE); clicks on the view arrive in rotated (display) coordinates
and simulated_target.py maps them back.
"""

import cv2
import numpy as np

# Degrees (counter-clockwise) -> OpenCV rotate code. The frame is rotated only
# for display AFTER drawing, so detection coordinates always match the camera.
_ROTATE = {
    90: cv2.ROTATE_90_COUNTERCLOCKWISE,
    180: cv2.ROTATE_180,
    270: cv2.ROTATE_90_CLOCKWISE,
}

_RED = (0, 0, 255)
_GREEN = (0, 220, 0)
_BLUE = (255, 160, 0)
_WHITE = (255, 255, 255)
_GREY = (140, 140, 140)


def _ui_scale(frame):
    """Overlay scale factor: 1.0 at 1280 px wide, clamped so it stays legible.

    Without this, text sized for a 720p camera frame covers half a small test
    image, and the button drawn on a 448 px screenshot is wider than the scene.
    """
    return max(0.35, min(1.4, frame.shape[1] / 1280.0))


def _age_text(age):
    """'0.3s ago' near real time, '12s ago' once it's worth rounding off."""
    if age is None:
        return ""
    return f"{age:.1f}s ago" if age < 10.0 else f"{age:.0f}s ago"


def draw_overlay(frame, red, targets, target, valid, rejects=()):
    """Annotate the frame with both detections and the error vector.

    `rejects` is [(Dot, reason)] from find_black_dots - the blobs that were the
    right size but were not round enough (or not dark enough) to be a dot.
    Drawn in grey with the measurement that failed, so a missed dot can be read
    off the frame: "circ 0.71" says raise nothing, it is not round; "pale 0.86"
    says the ink test is what to loosen.

    The numbers (error, fps, telemetry...) are not drawn here: see
    `status_lines`, shown in the left panel.
    """
    s = _ui_scale(frame)
    thick = max(1, int(2 * s))

    for d, reason in rejects:
        x1, y1, x2, y2 = d.bbox
        cv2.rectangle(frame, (x1, y1), (x2, y2), _GREY, 1)
        cv2.putText(frame, reason, (x1, max(int(10 * s), y1 - int(4 * s))),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.4 * s, _GREY, 1, cv2.LINE_AA)

    # Ring the candidates OUTSIDE their own edge - a circle drawn at the blob's
    # own radius is invisible against the ink it traces.
    for d in targets:
        cv2.circle(frame, d.center, d.radius + int(4 * s) + 1, _BLUE, 1)

    if red is not None:
        cv2.circle(frame, red.center, red.radius + int(6 * s) + 2, _RED, thick)
        cv2.drawMarker(frame, red.center, _RED, cv2.MARKER_CROSS, int(14 * s) + 4, 1)

    if target is not None:
        cv2.circle(frame, target.center, target.radius + int(6 * s) + 2, _GREEN, thick)
        cv2.drawMarker(frame, target.center, _GREEN, cv2.MARKER_TILTED_CROSS,
                       int(14 * s) + 4, 1)

    if valid:
        # The error vector itself: tail on the laser, head on the target.
        cv2.arrowedLine(frame, red.center, target.center, _WHITE, thick,
                        cv2.LINE_AA, tipLength=0.15)
    return frame


def status_lines(red, targets, target, dx, dy, valid, fps, frame_shape, link,
                 telemetry=None, telemetry_age=None, rejects=(),
                 recentering=False, sys_tlm=None, sys_age=None):
    """The status text for the left panel, one string per line.

    `sys_tlm` is a parsed `tlm.sys` line (serial_link.parse_tlm_sys, 1 Hz);
    its `pid_hz` is shown below the ESP T block, with its own age.

    `telemetry` is a parsed `tlm` sample (serial_link.parse_tlm) or None if
    none has arrived yet. `telemetry_age` is seconds since it was received -
    shown as "Xs ago" rather than hiding the sample once it goes stale, so a
    dead link reads as a growing age instead of the readout vanishing.

    The target's roundness is worth a slot: it is the one number that says
    how comfortably the chosen dot passed, and a target hovering near the
    threshold is what a flickering lock looks like from here.
    """
    lines = [
        f"red: {'YES' if red is not None else 'no'}",
        f"black: {len(targets)}"
        + (f" (+{len(rejects)} rejected)" if rejects else ""),
        "target: "
        + (f"YES roundness score {target.roundness:.2f}"
           if target is not None else "no"),
        f"E {dx:+.3f} {dy:+.3f} {1 if valid else 0}",
        f"{fps:.0f} fps {frame_shape[1]}x{frame_shape[0]}",
        f"sent {link.sent}   fired {link.fired}",
        f"port: {link.port or 'no port'}",
    ]
    if recentering:
        lines.insert(1, "laser lost - moving toward centre")
    if telemetry is not None:
        lines += [
            f"ESP T  st:{telemetry['st']} ch:{telemetry['ch']}   "
            f"{_age_text(telemetry_age)}",
            f"ex:{telemetry['ex']:+.3f} ey:{telemetry['ey']:+.3f}",
            f"v:{telemetry['vp']:+.1f}/{telemetry['vt']:+.1f} deg/s",
            f"pan:{telemetry['pan']:.1f} tilt:{telemetry['tilt']:.1f}",
        ]
    if sys_tlm is not None and "pid_hz" in sys_tlm:
        lines.append(f"PID: {sys_tlm['pid_hz']:.1f} Hz   {_age_text(sys_age)}")
    return lines


def render_masks(red_mask, black_mask, rotate):
    """One BGR image with both binary masks side by side, for threshold tuning."""
    both = np.hstack([red_mask, black_mask])
    scale = 900.0 / both.shape[1]
    if scale < 1.0:
        both = cv2.resize(both, None, fx=scale, fy=scale)
    both = cv2.cvtColor(both, cv2.COLOR_GRAY2BGR)
    cv2.putText(both, "red", (8, 24), cv2.FONT_HERSHEY_SIMPLEX, 0.7, _RED, 2)
    cv2.putText(both, "black", (both.shape[1] // 2 + 8, 24),
                cv2.FONT_HERSHEY_SIMPLEX, 0.7, _GREEN, 2)
    if rotate:
        both = cv2.rotate(both, _ROTATE[rotate])
    return both
