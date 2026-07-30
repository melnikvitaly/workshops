"""Tidy silkscreen: shrink reference text, drop silk that falls off the board,
and nudge each reference designator to a spot that clears pads and other silk."""
import math
import pcbnew

PCB = r'C:\Projects\embedded\workshops\workshop-5-1\kicad\kicad.kicad_pcb'
MM, TO = pcbnew.FromMM, pcbnew.ToMM

X0, Y0, X1, Y1 = 20.0, 25.0, 75.0, 70.0
PAD_GAP = 0.18
SILK_GAP = 0.12
EDGE_GAP = 0.15
TEXT_H, TEXT_T = 0.8, 0.12


def box(item):
    """bbox as [l,t,r,b] in mm - must be read while the owning iterator is live"""
    bb = item.GetBoundingBox()
    return [TO(bb.GetLeft()), TO(bb.GetTop()), TO(bb.GetRight()), TO(bb.GetBottom())]


def grow(r, g):
    return [r[0] - g, r[1] - g, r[2] + g, r[3] + g]


def hits(a, b):
    return not (a[2] <= b[0] or b[2] <= a[0] or a[3] <= b[1] or b[3] <= a[1])


HOLES = [(23.5, 28.5), (71.5, 28.5), (23.5, 66.5), (71.5, 66.5)]
HOLE_R = 1.1


def inside_board(r):
    if not (r[0] >= X0 + EDGE_GAP and r[2] <= X1 - EDGE_GAP and
            r[1] >= Y0 + EDGE_GAP and r[3] <= Y1 - EDGE_GAP):
        return False
    for hx, hy in HOLES:                       # mounting holes are board edges too
        dx = max(r[0] - hx, hx - r[2], 0.0)
        dy = max(r[1] - hy, hy - r[3], 0.0)
        if math.hypot(dx, dy) < HOLE_R + EDGE_GAP:
            return False
    return True


def main():
    b = pcbnew.LoadBoard(PCB)
    fps = [f for f in b.GetFootprints()]

    # ---- 1. collect silk graphics + their boxes in one live pass ----
    silk_items = []          # (footprint, item, box)
    pad_boxes = []
    for fp in fps:
        for g in fp.GraphicalItems():
            if g.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):
                silk_items.append((fp, g, box(g)))
        for p in fp.Pads():
            if p.IsOnLayer(pcbnew.F_Cu):
                pad_boxes.append(grow(box(p), PAD_GAP))

    # ---- 2. drop silk that leaves the board (WROOM antenna overhang) ----
    dropped = 0
    keep = []
    for fp, g, r in silk_items:
        if (r[0] < X0 or r[2] > X1 or r[1] < Y0 or r[3] > Y1):
            fp.Remove(g)
            dropped += 1
        else:
            keep.append(r)
    print(f'silk graphics removed (off board): {dropped}')
    silk_boxes = [grow(r, SILK_GAP) for r in keep]

    # ---- 3. reference designators ----
    refs = []
    for fp in fps:
        f = fp.Reference()
        if not f.IsVisible() or f.GetLayer() != pcbnew.F_SilkS:
            continue
        f.SetTextSize(pcbnew.VECTOR2I(MM(TEXT_H), MM(TEXT_H)))
        f.SetTextThickness(MM(TEXT_T))
        f.SetTextAngleDegrees(0)
        cb = fp.GetCourtyard(pcbnew.F_CrtYd).BBox()
        hw = max(TO(cb.GetWidth()) / 2, 0.6)
        hh = max(TO(cb.GetHeight()) / 2, 0.5)
        if fp.GetReference() == 'U1':      # courtyard includes the antenna keepout
            hw, hh = 9.6, 13.2
        tb = box(f)
        refs.append((fp, f, hw, hh,
                     (tb[2] - tb[0]) / 2, (tb[3] - tb[1]) / 2,
                     TO(fp.GetPosition().x), TO(fp.GetPosition().y)))

    refs.sort(key=lambda t: -(t[2] * t[3]))     # biggest parts get first pick

    placed, moved, failed = [], 0, []
    for fp, f, hw, hh, tw2, th2, cx, cy in refs:
        best = None
        for extra in (0.3, 0.55, 0.85, 1.2, 1.7, 2.3, 3.0, 4.0):
            for ang in range(0, 360, 15):
                a = math.radians(ang)
                px = cx + math.cos(a) * (hw + tw2 + extra)
                py = cy + math.sin(a) * (hh + th2 + extra)
                r = [px - tw2, py - th2, px + tw2, py + th2]
                if not inside_board(r):
                    continue
                rg = grow(r, SILK_GAP)
                if any(hits(r, pb) for pb in pad_boxes):
                    continue
                if any(hits(rg, sb) for sb in silk_boxes):
                    continue
                if any(hits(rg, ob) for ob in placed):
                    continue
                best = (px, py, rg)
                break
            if best:
                break
        if best:
            px, py, rg = best
            f.SetPosition(pcbnew.VECTOR2I(MM(px), MM(py)))
            placed.append(rg)
            moved += 1
        else:
            failed.append(fp.GetReference())
            f.SetVisible(False)          # nowhere legible - hide rather than clash

    print(f'references placed: {moved}/{len(refs)}')
    if failed:
        print('  hidden (no clear spot):', ', '.join(failed))

    pcbnew.SaveBoard(PCB, b)
    print('saved')


if __name__ == '__main__':
    main()
