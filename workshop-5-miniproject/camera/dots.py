"""Classic-CV detection of the two dots the loop needs.

  red dot   -- the laser / red marker: where we are pointing NOW
  black dot -- the printed target on the paper: where we WANT to point

Both are found with plain OpenCV on the host, not a neural net. The camera-host
project (../../../final_project/camera-host) runs a fixed-weight NN for the red
dot because the OAK's VPU cannot execute OpenCV; here detection runs on the PC,
so the color filter can just be a color filter.

Shape of the job for each color:

    mask -> external contours -> keep blobs that are the right SIZE and ROUND
         -> score the survivors -> best one wins

Area thresholds are quoted for a 640x480 reference frame and auto-scaled to the
actual resolution (a dot's pixel area grows ~quadratically with linear size),
the same convention reddot_host.reduce_heatmap uses.
"""

import math
from dataclasses import dataclass

import cv2
import numpy as np

_REF_AREA = 640 * 480
_K3 = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))


@dataclass(frozen=True)
class Dot:
    """One accepted blob, in pixel coordinates of the frame it was found in."""

    x: float
    y: float
    area: float
    circularity: float
    bbox: tuple  # (x1, y1, x2, y2)

    @property
    def center(self):
        return (int(round(self.x)), int(round(self.y)))

    @property
    def radius(self):
        return max(3, int(round(math.sqrt(max(self.area, 1.0) / math.pi))))


def _scale_area(area_min, area_max, h, w):
    scale = (h * w) / _REF_AREA
    return area_min * scale, area_max * scale


def _candidates(mask, area_min, area_max, min_circ, min_fill):
    """Contours of `mask` that pass the size / roundness / fill gates."""
    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL,
                                   cv2.CHAIN_APPROX_SIMPLE)
    out = []
    for c in contours:
        area = cv2.contourArea(c)
        if area < area_min or area > area_max:
            continue
        (cx, cy), radius = cv2.minEnclosingCircle(c)
        circularity = area / (math.pi * radius * radius + 1e-6)
        if circularity < min_circ:
            continue
        x, y, bw, bh = cv2.boundingRect(c)
        # A round blob fills ~78% of its bounding box; a stroke of text or the
        # edge of a shadow fills far less even when it sneaks past circularity.
        if area < min_fill * bw * bh:
            continue
        out.append((Dot(cx, cy, area, circularity, (x, y, x + bw, y + bh)), c))
    return out


def _contour_mask(shape, contour):
    m = np.zeros(shape[:2], np.uint8)
    cv2.drawContours(m, [contour], -1, 255, -1)
    return m


# --- red dot ---------------------------------------------------------------

def redness_map(frame_bgr):
    """R - max(G, B): how red a pixel is, independent of how bright it is."""
    b, g, r = cv2.split(frame_bgr)
    return cv2.subtract(r, cv2.max(g, b))   # uint8, clipped at 0


def find_red_dot(frame_bgr, area_min=3, area_max=2000, min_circ=0.35,
                 min_redness=22, rel=0.5):
    """Best red blob in the frame. Returns (Dot or None, mask).

    Thresholding is on REDNESS (R - max(G,B)), not on HSV saturation. A laser
    dot is the brightest thing in the frame and its core clips to white: a real
    dot measures around S=50..120 with a near-white centre, so any saturation
    gate high enough to reject warm-coloured clutter also rejects the dot
    itself. Redness has no such problem - white is 0, and only genuinely red
    pixels score.

    The threshold is relative to the frame's own peak (`rel` x peak), with
    `min_redness` as an absolute floor so that a frame containing no dot at all
    finds nothing instead of latching onto the reddest noise. Candidates are
    scored by circularity x peak redness.
    """
    h, w = frame_bgr.shape[:2]
    amin, amax = _scale_area(area_min, area_max, h, w)

    redness = redness_map(frame_bgr)
    peak = float(redness.max())
    if peak < min_redness:
        return None, np.zeros((h, w), np.uint8)      # nothing red in frame

    thr = max(min_redness, rel * peak)
    mask = (redness >= thr).astype(np.uint8) * 255
    # Close only - no opening. A dot can be 3 px across at this range, and an
    # open would erase it; isolated speckles are dropped by area_min instead.
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, _K3)

    best, best_score = None, 0.0
    for dot, contour in _candidates(mask, amin, amax, min_circ, 0.35):
        blob_peak = float(redness[_contour_mask(frame_bgr.shape, contour) > 0].max())
        score = dot.circularity * blob_peak
        if score > best_score:
            best, best_score = dot, score
    return best, mask


# --- black printed dots ----------------------------------------------------

def _ring_stats(hsv, contour, bbox, pad):
    """(blob median, surrounding-ring median) of V and S around one candidate.

    Every judgement about a printed dot has to be made against the paper right
    next to it, never against a fixed number: a scene under a coloured lamp
    shifts everything at once. Here the blob is compared with a ring of
    background just outside it.
    """
    h, w = hsv.shape[:2]
    x1, y1, x2, y2 = bbox
    x1p, y1p = max(0, x1 - pad), max(0, y1 - pad)
    x2p, y2p = min(w, x2 + pad), min(h, y2 + pad)
    roi = hsv[y1p:y2p, x1p:x2p]
    if roi.size == 0:
        return None

    blob = np.zeros(roi.shape[:2], np.uint8)
    cv2.drawContours(blob, [contour], -1, 255, -1, offset=(-x1p, -y1p))
    ring = cv2.dilate(blob, np.ones((pad * 2 + 1,) * 2, np.uint8)) & ~blob
    if not blob.any() or not ring.any():
        return None

    v, s = roi[:, :, 2], roi[:, :, 1]
    return (float(np.median(v[blob > 0])), float(np.median(v[ring > 0])),
            float(np.median(s[blob > 0])), float(np.median(s[ring > 0])))


def find_black_dots(frame_bgr, area_min=40, area_max=20000, min_circ=0.55,
                    darkness=0.8, sat_margin=70, block=51, offset=12):
    """All black printed dots in the frame. Returns (list of Dot, mask).

    Ink is only "black" relative to the paper around it, so every test here is
    relative:

      adaptive threshold -- darker than the local mean by `offset`, which
        survives uneven lighting across the sheet;
      darkness -- the blob's median V must be at most `darkness` x the median V
        of the ring of paper just around it. Rejects a light-grey smudge that
        the adaptive step accepted;
      sat_margin -- the blob must not be much more saturated than that same
        ring. This is what separates ink from a coloured object, and it has to
        be relative: under a blue lamp the paper itself measures S~105 and the
        ink S~135, so any absolute "ink is unsaturated" rule throws the real
        dot away. A red sticker on white paper, by contrast, sits ~170 above
        its background and is rejected.

    The list is sorted largest-first; pick_target chooses among them.
    """
    h, w = frame_bgr.shape[:2]
    amin, amax = _scale_area(area_min, area_max, h, w)

    hsv = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2HSV)
    gray = cv2.GaussianBlur(hsv[:, :, 2], (5, 5), 0)
    block = block if block % 2 else block + 1        # must be odd
    mask = cv2.adaptiveThreshold(gray, 255, cv2.ADAPTIVE_THRESH_MEAN_C,
                                 cv2.THRESH_BINARY_INV, block, offset)
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, _K3)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, _K3)

    dots = []
    for dot, contour in _candidates(mask, amin, amax, min_circ, 0.55):
        stats = _ring_stats(hsv, contour, dot.bbox, max(3, dot.radius // 2))
        if stats is None:
            continue
        blob_v, ring_v, blob_s, ring_s = stats
        if blob_v > darkness * ring_v:
            continue                      # not appreciably darker than paper
        if blob_s > ring_s + sat_margin:
            continue                      # a coloured object, not ink
        dots.append(dot)

    dots.sort(key=lambda d: d.area, reverse=True)
    return dots, mask


def pick_target(dots, frame_shape, mode="center", red=None):
    """Choose which black dot is THE target when several are printed.

      center  -- nearest the frame centre: aim the camera to choose (default)
      largest -- biggest blob: the nearest / boldest dot
      nearest -- nearest the red dot: shortest correction, needs a red dot
    """
    if not dots:
        return None
    if mode == "largest":
        return dots[0]                                # already sorted by area
    if mode == "nearest" and red is not None:
        return min(dots, key=lambda d: math.hypot(d.x - red.x, d.y - red.y))
    h, w = frame_shape[:2]
    return min(dots, key=lambda d: math.hypot(d.x - w / 2.0, d.y - h / 2.0))


# --- the error vector ------------------------------------------------------

def error_vector(red, target, frame_shape):
    """Normalised error from the RED dot to the BLACK dot: (dx, dy, valid).

        error = target - laser_dot

    normalised so +-1.0 spans HALF the frame in that axis, which is exactly what
    docs/uart-protocol.md specifies. Y keeps the image's native downward
    direction; the firmware flips it via TILT_INVERT if the servo needs it.
    Returns (0.0, 0.0, False) unless both dots were seen this frame.
    """
    if red is None or target is None:
        return 0.0, 0.0, False
    h, w = frame_shape[:2]
    return ((target.x - red.x) / (w / 2.0),
            (target.y - red.y) / (h / 2.0), True)
