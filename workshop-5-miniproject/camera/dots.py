"""Classic-CV detection of the two dots the loop needs.

  red dot   -- the laser / red marker: where we are pointing NOW
  black dot -- the printed target on the paper: where we WANT to point

Both are found with plain OpenCV on the host, not a neural net. The camera-host
project (../../../final_project/camera-host) runs a fixed-weight NN for the red
dot because the OAK's VPU cannot execute OpenCV; here detection runs on the PC,
so the color filter can just be a color filter.

Shape of the job for each color:

    mask -> contours -> keep blobs that are the right SIZE and ROUND
         -> score the survivors -> best one wins

"ROUND" is where the work is. Dark is common - shadows, text, a cable, the edge
of the sheet - so the black dot is put through six independent shape
measurements (`_measure`), each one blind to a different way of not being a
circle; the red dot is only ever one thing in frame and is judged loosely.

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

# A disc fills pi/4 of its bounding box - the neutral default for `Dot.extent`
# on a dot that was placed rather than measured (the simulated target).
_CIRCLE_EXTENT = math.pi / 4.0


@dataclass(frozen=True)
class Dot:
    """One accepted blob, in pixel coordinates of the frame it was found in.

    Beyond position and size it carries the six shape measurements the round
    test is made of, so a rejection can be explained instead of just happening
    (see `_shape_reject`, and the labelled grey boxes --debug draws).
    """

    x: float
    y: float
    area: float
    circularity: float      # area / area of the min enclosing circle
    bbox: tuple             # (x1, y1, x2, y2)
    compactness: float = 1.0    # 4*pi*A / P^2  -- 1.0 for a circle
    solidity: float = 1.0       # area / convex hull area
    extent: float = _CIRCLE_EXTENT   # area / bounding-box area
    aspect: float = 1.0         # long / short side of the min-area rect
    radial_cv: float = 0.0      # spread of centre->edge distance, /mean
    hole_ratio: float = 0.0     # enclosed background / outer area

    @property
    def center(self):
        return (int(round(self.x)), int(round(self.y)))

    @property
    def radius(self):
        return max(3, int(round(math.sqrt(max(self.area, 1.0) / math.pi))))

    @property
    def roundness(self):
        """0..1 summary: the worst of the shape measurements.

        Display and ranking only - acceptance uses the individual gates, which
        each have their own threshold. A printed dot scores 0.82-0.98, a square
        0.65, a ring 0.57, a letter or a shadow edge well below that.
        """
        return max(0.0, min(self.circularity, self.compactness, self.solidity,
                            1.0 - 2.5 * self.radial_cv, 1.0 - self.hole_ratio,
                            1.0 / max(self.aspect, 1e-6)))


def _scale_area(area_min, area_max, h, w):
    scale = (h * w) / _REF_AREA
    return area_min * scale, area_max * scale


def _measure(contour, area, hole_area=0.0):
    """Turn one contour into a Dot with every shape measurement filled in.

    No single number separates a printed dot from everything else that happens
    to be dark, so six are taken, each blind to a different impostor:

      circularity -- fraction of the min enclosing circle that is filled. The
        broad one: catches squares (0.64), triangles (0.41), anything sticking
        out on one side.
      compactness -- 4*pi*A / P^2, perimeter against area. Catches ragged and
        knobbly outlines whose area is fine but whose edge is long: shadow
        edges, joined-up text, cloth texture.
      solidity -- area against its own convex hull. Catches dents and notches:
        two dots touching, a dot with a bite out of it, a C or an L.
      aspect -- long/short side of the minimum-area rectangle. Catches
        ellipses and rounded bars, which can pass all of the above.
      radial_cv -- spread of the centre-to-edge distance. The direct question:
        is every point on the outline the same distance from the middle? This
        is what a circle *is*, and it is the one an axis-aligned rounded square
        cannot fake.
      hole_ratio -- how much enclosed background the outline contains. Every
        measurement above is taken on the OUTLINE, so a ring, an O, a washer or
        a bottle cap seen from above are perfect circles by all of them. A
        printed dot is solid; this is the only test that can tell the
        difference.

    Contours are traced with CHAIN_APPROX_NONE (see `_candidates`) so the
    radial measurement sees the whole outline, not four compressed corners.
    """
    (cx, cy), radius = cv2.minEnclosingCircle(contour)
    perimeter = cv2.arcLength(contour, True)
    hull_area = cv2.contourArea(cv2.convexHull(contour))
    x, y, bw, bh = cv2.boundingRect(contour)
    (_, _), (rw, rh), _ = cv2.minAreaRect(contour)

    pts = contour.reshape(-1, 2).astype(np.float64)
    dist = np.hypot(pts[:, 0] - cx, pts[:, 1] - cy)

    return Dot(
        x=cx, y=cy, area=area,
        circularity=area / (math.pi * radius * radius + 1e-6),
        bbox=(x, y, x + bw, y + bh),
        compactness=min(1.0, 4.0 * math.pi * area / (perimeter * perimeter + 1e-6)),
        solidity=area / (hull_area + 1e-6),
        extent=area / float(bw * bh + 1e-6),
        aspect=max(rw, rh) / max(min(rw, rh), 1e-6),
        radial_cv=float(dist.std() / (dist.mean() + 1e-6)),
        hole_ratio=hole_area / (area + 1e-6),
    )


def _candidates(mask, area_min, area_max):
    """Every outer contour of `mask` inside the size window, measured.

    Two choices here exist only to make the shape tests mean something:

    CHAIN_APPROX_NONE, not SIMPLE -- SIMPLE compresses straight runs to their
    endpoints, which leaves a square as four corner points, all of them the
    same distance from the centre. It would score a perfect radial_cv.

    RETR_CCOMP, not EXTERNAL -- EXTERNAL discards the inner contours, and the
    holes are the only thing distinguishing a printed dot from a ring. The
    top-level contours it walks are the same ones EXTERNAL would return, so
    nothing else changes; `area` stays the filled outer area, with the enclosed
    background reported separately as hole_ratio.
    """
    contours, hierarchy = cv2.findContours(mask, cv2.RETR_CCOMP,
                                           cv2.CHAIN_APPROX_NONE)
    if hierarchy is None:
        return []
    hierarchy = hierarchy[0]        # [next, prev, first_child, parent]

    out = []
    for i, c in enumerate(contours):
        if hierarchy[i][3] != -1:   # a hole; accounted for by its parent
            continue
        area = cv2.contourArea(c)
        if area < area_min or area > area_max or len(c) < 3:
            continue
        hole_area = 0.0
        child = hierarchy[i][2]
        while child != -1:
            hole_area += cv2.contourArea(contours[child])
            child = hierarchy[child][0]
        out.append((_measure(c, area, hole_area), c))
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

    # The red gate stays loose on purpose: at range the dot is a handful of
    # pixels, where the shape measurements the black dot is judged on are all
    # noise. Size, circularity and a light fill check are as much as a 3 px
    # blob can honestly support - and there is only ever one red thing in
    # frame, so the scoring below settles it anyway.
    best, best_score = None, 0.0
    for dot, contour in _candidates(mask, amin, amax):
        if dot.circularity < min_circ or dot.extent < 0.35:
            continue
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


def _shape_reject(dot, min_circ, max_radial, max_aspect, min_solidity,
                  min_compact, max_hole):
    """None if `dot` is round enough to be a printed dot, else why it is not.

    Each gate is an independent way of failing to be a circle, so the first one
    that trips is the answer, and they are ordered by how much they explain.
    Thresholds come from measuring rendered discs against near-misses at
    radii 7-45 px; every one is overridable from the command line (the
    --black-* flags in detect_dots.py). Measured, disc vs. worst impostor:

        circularity   disc 0.82-0.98   square 0.72  ellipse(0.78) 0.77
        radial_cv     disc 0.00-0.10   square 0.11  rounded rect  0.13
        aspect        disc 1.00-1.12   ellipse 1.19-1.64  bar 2.4
        solidity      disc 0.88-0.99   dented disc 0.92   blob 0.79
        compactness   disc 0.45-0.91   frayed blob 0.42   triangle 0.57
    """
    if dot.circularity < min_circ:
        return f"circ {dot.circularity:.2f}"
    if dot.radial_cv > max_radial:
        return f"radial {dot.radial_cv:.2f}"
    if dot.aspect > max_aspect:
        return f"aspect {dot.aspect:.2f}"
    if dot.solidity < min_solidity:
        return f"solid {dot.solidity:.2f}"
    if dot.compactness < min_compact:
        return f"compact {dot.compactness:.2f}"
    if dot.hole_ratio > max_hole:
        return f"hollow {dot.hole_ratio:.2f}"
    # A disc fills pi/4 = 0.785 of its bounding box. Comfortably above that is
    # a square or a filled rectangle (0.88-0.98 measured); far below is a shape
    # that only reaches the corners of its box. The window is wide because a
    # small blob's bounding box is quantised - a 7 px disc measures 0.60.
    if not 0.55 <= dot.extent <= 0.90:
        return f"extent {dot.extent:.2f}"
    return None


def _touches_edge(dot, h, w, margin):
    """True if the blob runs into the frame border.

    A dot cut off by the edge is only a partial outline: the missing side makes
    it measure as some other shape entirely, and it cannot be aimed at anyway
    because its real centre is outside the picture.
    """
    x1, y1, x2, y2 = dot.bbox
    return (x1 <= margin or y1 <= margin
            or x2 >= w - margin or y2 >= h - margin)


def find_black_dots(frame_bgr, area_min=40, area_max=20000, min_circ=0.80,
                    darkness=0.8, sat_margin=70, block=51, offset=12,
                    max_radial=0.10, max_aspect=1.25, min_solidity=0.88,
                    min_compact=0.50, max_hole=0.15, edge_margin=2):
    """All black printed dots in the frame. Returns (list of Dot, mask, rejects).

    `rejects` is [(Dot, reason)] for blobs that were the right size but failed a
    later test - what --debug draws in grey so a miss can be read off the frame
    instead of guessed at.

    Two independent questions are asked of every blob.

    Is it ink? Ink is only "black" relative to the paper around it, so each test
    is relative:

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

    Is it a circle? `_shape_reject` runs the six measurements from `_measure`.
    Being dark is easy - shadows, text, a cable, the edge of the sheet and the
    gap under a clip all manage it - so the shape gate is the one doing the
    real work, and it is deliberately tight: a printed dot passes all six with
    room to spare, and near-misses like a square, a ring or a rounded bar fail
    at least one badly.

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

    dots, rejects = [], []
    for dot, contour in _candidates(mask, amin, amax):
        if edge_margin >= 0 and _touches_edge(dot, h, w, edge_margin):
            rejects.append((dot, "edge"))
            continue
        reason = _shape_reject(dot, min_circ, max_radial, max_aspect,
                               min_solidity, min_compact, max_hole)
        if reason is not None:
            rejects.append((dot, reason))
            continue
        stats = _ring_stats(hsv, contour, dot.bbox, max(3, dot.radius // 2))
        if stats is None:
            rejects.append((dot, "no ring"))
            continue
        blob_v, ring_v, blob_s, ring_s = stats
        if blob_v > darkness * ring_v:
            rejects.append((dot, f"pale {blob_v / (ring_v + 1e-6):.2f}"))
            continue                      # not appreciably darker than paper
        if blob_s > ring_s + sat_margin:
            rejects.append((dot, f"sat +{blob_s - ring_s:.0f}"))
            continue                      # a coloured object, not ink
        dots.append(dot)

    dots.sort(key=lambda d: d.area, reverse=True)
    return dots, mask, rejects


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
