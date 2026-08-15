"""Frame drawing for detect_dots: detections, error vector, and mask windows.

Everything here is display-only. Detection coordinates are always in original
camera space, so the overlay is drawn BEFORE any --rotate is applied to the
view (see _ROTATE); the FIRE button in fire_button.py is drawn after, because
its clicks arrive in window coordinates.
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

# Window titles. HighGUI addresses windows by title, so every imshow /
# namedWindow / setMouseCallback for the same window must use the same string.
_WIN = "dots: red -> black"
_CONTROLS_WIN = "controls"
_MASK_WIN = "masks (debug)"


def _ui_scale(frame):
    """Overlay scale factor: 1.0 at 1280 px wide, clamped so it stays legible.

    Without this, text sized for a 720p camera frame covers half a small test
    image, and the button drawn on a 448 px screenshot is wider than the scene.
    """
    return max(0.35, min(1.4, frame.shape[1] / 1280.0))


def draw_overlay(frame, red, targets, target, dx, dy, valid, fps, link, esp=""):
    """Annotate the frame with both detections and the error vector."""
    s = _ui_scale(frame)
    thick = max(1, int(2 * s))

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

    lines = [
        f"red: {'YES' if red is not None else 'no'}   "
        f"black: {len(targets)}   target: {'YES' if target is not None else 'no'}",
        f"E {dx:+.3f} {dy:+.3f} {1 if valid else 0}   "
        f"{fps:.0f} fps   sent {link.sent}   fired {link.fired}   "
        f"{link.port or 'no port'}",
    ]
    if esp:
        # The newest line the firmware logged back on the shared UART - usually
        # its own view of the same error, which makes a sign or scaling mistake
        # obvious at a glance.
        lines.append("esp32 | " + esp[:78])
    for i, text in enumerate(lines):
        org = (int(10 * s) + 2, int((30 + 26 * i) * s) + 8)
        cv2.putText(frame, text, org, cv2.FONT_HERSHEY_SIMPLEX, 0.65 * s,
                    _WHITE, max(1, int(2 * s)), cv2.LINE_AA)
    return frame


def show_masks(red_mask, black_mask, rotate):
    """One window with both binary masks side by side, for threshold tuning."""
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
    cv2.imshow(_MASK_WIN, both)


def hide_masks():
    """Close the mask window (no-op if it was never opened)."""
    try:
        cv2.destroyWindow(_MASK_WIN)
    except cv2.error:
        pass
