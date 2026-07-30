"""GND pours on F.Cu/B.Cu + stitching vias."""
import math
import numpy as np
import pcbnew

PCB = r'C:\Projects\embedded\workshops\workshop-5-1\kicad\kicad.kicad_pcb'
MM, TO = pcbnew.FromMM, pcbnew.ToMM

GRID = 0.05
X0, Y0, X1, Y1 = 20.0, 25.0, 75.0, 70.0
CORNER_R = 2.0
HOLES = [(23.5, 28.5), (71.5, 28.5), (23.5, 66.5), (71.5, 66.5)]
HOLE_R = 1.1
CLEAR, EDGE_CLEAR = 0.25, 0.55   # incl. margin for grid discretisation
VIA_D, VIA_DRILL = 0.6, 0.3
STITCH_PITCH = 4.0
EPS = 1e-7

W = int(round((X1 - X0) / GRID)) + 1
H = int(round((Y1 - Y0) / GRID)) + 1


def gx(x): return (x - X0) / GRID
def gy(y): return (y - Y0) / GRID


def srange(lo, hi, n):
    return max(int(math.ceil(lo + EPS)), 0), min(int(math.floor(hi - EPS)), n - 1)


def rect_cells(xl, yl, xh, yh):
    i0, i1 = srange(gx(xl), gx(xh), W)
    j0, j1 = srange(gy(yl), gy(yh), H)
    if i1 < i0 or j1 < j0:
        return None
    return j0, j1 + 1, i0, i1 + 1


def outline_pts(inset):
    """rounded-rect board outline, inset inward"""
    r = CORNER_R - inset
    x0, y0 = X0 + inset, Y0 + inset
    x1, y1 = X1 - inset, Y1 - inset
    pts = []
    corners = [(x0 + r, y0 + r, 180, 270), (x1 - r, y0 + r, 270, 360),
               (x1 - r, y1 - r, 0, 90), (x0 + r, y1 - r, 90, 180)]
    for cx, cy, a0, a1 in corners:
        for k in range(9):
            a = math.radians(a0 + (a1 - a0) * k / 8)
            pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def main():
    b = pcbnew.LoadBoard(PCB)

    ni = b.GetNetInfo()
    gnd = None
    for i in range(b.GetNetCount()):
        n = ni.GetNetItem(i)
        if n and n.GetNetname() == 'GND':
            gnd = n.GetNetCode()
    assert gnd is not None, 'no GND net'

    # drop previously generated zones and stitching vias so re-running is
    # idempotent (GND is never routed, so every GND via here is one we added)
    for z in list(b.Zones()):
        if not z.GetIsRuleArea():
            b.Remove(z)

    # ---------- stitching vias ----------
    half = VIA_D / 2
    blocked = np.zeros((2, H, W), dtype=bool)
    m = EDGE_CLEAR + half
    i0, i1 = srange(-1e9, gx(X0 + m), W); blocked[:, :, i0:i1 + 1] = True
    i0, i1 = srange(gx(X1 - m), 1e9, W);  blocked[:, :, i0:i1 + 1] = True
    j0, j1 = srange(-1e9, gy(Y0 + m), H); blocked[:, j0:j1 + 1, :] = True
    j0, j1 = srange(gy(Y1 - m), 1e9, H);  blocked[:, j0:j1 + 1, :] = True
    for ccx, ccy, sx, sy in [(X0 + CORNER_R, Y0 + CORNER_R, -1, -1),
                             (X1 - CORNER_R, Y0 + CORNER_R, 1, -1),
                             (X1 - CORNER_R, Y1 - CORNER_R, 1, 1),
                             (X0 + CORNER_R, Y1 - CORNER_R, -1, 1)]:
        r = CORNER_R - m
        sl = rect_cells(min(ccx, ccx + sx * CORNER_R) - .1, min(ccy, ccy + sy * CORNER_R) - .1,
                        max(ccx, ccx + sx * CORNER_R) + .1, max(ccy, ccy + sy * CORNER_R) + .1)
        if sl:
            jj0, jj1, ii0, ii1 = sl
            ii = np.arange(ii0, ii1) * GRID + X0 - ccx
            jj = np.arange(jj0, jj1) * GRID + Y0 - ccy
            out = (ii[None, :] ** 2 + jj[:, None] ** 2) > r * r
            blocked[0, jj0:jj1, ii0:ii1] |= out
            blocked[1, jj0:jj1, ii0:ii1] |= out
    for hx, hy in HOLES:
        rr = HOLE_R + EDGE_CLEAR + half
        sl = rect_cells(hx - rr, hy - rr, hx + rr, hy + rr)
        if sl:
            jj0, jj1, ii0, ii1 = sl
            ii = np.arange(ii0, ii1) * GRID + X0 - hx
            jj = np.arange(jj0, jj1) * GRID + Y0 - hy
            msk = (ii[None, :] ** 2 + jj[:, None] ** 2) < rr * rr
            blocked[0, jj0:jj1, ii0:ii1] |= msk
            blocked[1, jj0:jj1, ii0:ii1] |= msk

    infl = CLEAR + half
    for fp in b.GetFootprints():
        for p in fp.Pads():                      # never sit a stitch via on any pad
            bb = p.GetBoundingBox()
            sl = rect_cells(TO(bb.GetLeft()) - infl, TO(bb.GetTop()) - infl,
                            TO(bb.GetRight()) + infl, TO(bb.GetBottom()) + infl)
            if sl:
                jj0, jj1, ii0, ii1 = sl
                blocked[0, jj0:jj1, ii0:ii1] = True
                blocked[1, jj0:jj1, ii0:ii1] = True
    for t in b.GetTracks():
        # existing GND vias still block new via placement, so re-running is safe
        if t.GetNetCode() == gnd and not isinstance(t, pcbnew.PCB_VIA):
            continue
        s, e = t.GetStart(), t.GetEnd()
        x1_, y1_, x2_, y2_ = TO(s.x), TO(s.y), TO(e.x), TO(e.y)
        if isinstance(t, pcbnew.PCB_VIA):
            r = TO(t.GetWidth(pcbnew.F_Cu)) / 2 + infl
            x2_, y2_ = x1_, y1_
        else:
            r = TO(t.GetWidth()) / 2 + infl
        n = max(1, int(math.hypot(x2_ - x1_, y2_ - y1_) / (GRID * 0.7)) + 1)
        for k in range(n + 1):
            tt = k / n
            cx, cy = x1_ + (x2_ - x1_) * tt, y1_ + (y2_ - y1_) * tt
            sl = rect_cells(cx - r, cy - r, cx + r, cy + r)
            if not sl:
                continue
            jj0, jj1, ii0, ii1 = sl
            ii = np.arange(ii0, ii1) * GRID + X0 - cx
            jj = np.arange(jj0, jj1) * GRID + Y0 - cy
            msk = (ii[None, :] ** 2 + jj[:, None] ** 2) < r * r
            L = 0 if t.IsOnLayer(pcbnew.F_Cu) else 1
            if isinstance(t, pcbnew.PCB_VIA):
                blocked[0, jj0:jj1, ii0:ii1] |= msk
                blocked[1, jj0:jj1, ii0:ii1] |= msk
            else:
                blocked[L, jj0:jj1, ii0:ii1] |= msk

    free = ~(blocked[0] | blocked[1])

    def put_via(x, y):
        v = pcbnew.PCB_VIA(b)
        v.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
        v.SetWidth(MM(VIA_D)); v.SetDrill(MM(VIA_DRILL))
        v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
        v.SetNetCode(gnd)
        b.Add(v)
        rr = VIA_D / 2 + CLEAR
        sl = rect_cells(x - rr, y - rr, x + rr, y + rr)
        if sl:
            jj0, jj1, ii0, ii1 = sl
            ii = np.arange(ii0, ii1) * GRID + X0 - x
            jj = np.arange(jj0, jj1) * GRID + Y0 - y
            free[jj0:jj1, ii0:ii1] &= ~((ii[None, :] ** 2 + jj[:, None] ** 2) < rr * rr)

    # 1. a dedicated via beside every SMD GND pad so each ground pad reaches the
    #    B.Cu plane directly instead of relying on an F.Cu island surviving
    dirs = [(math.cos(a), math.sin(a))
            for a in (math.radians(k * 30) for k in range(12))]
    npad, missed = 0, []
    for fp in b.GetFootprints():
        for p in fp.Pads():
            if p.GetNetCode() != gnd or p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
                continue
            bb = p.GetBoundingBox()
            cx, cy = TO(p.GetPosition().x), TO(p.GetPosition().y)
            hw = TO(bb.GetRight() - bb.GetLeft()) / 2
            hh = TO(bb.GetBottom() - bb.GetTop()) / 2
            placed = False
            for d in (0.0, 0.15, 0.3, 0.5, 0.75, 1.0, 1.4, 1.8):
                for dx, dy in dirs:
                    vx = cx + dx * (hw + VIA_D / 2 + CLEAR + d)
                    vy = cy + dy * (hh + VIA_D / 2 + CLEAR + d)
                    i, j = int(round(gx(vx))), int(round(gy(vy)))
                    if 0 <= i < W and 0 <= j < H and free[j, i]:
                        put_via(X0 + i * GRID, Y0 + j * GRID)
                        npad += 1
                        placed = True
                        break
                if placed:
                    break
            if not placed:
                missed.append(f'{fp.GetReference()}.{p.GetNumber()}')
    print(f'GND pad vias: {npad}')
    if missed:
        print('  no room beside:', ', '.join(missed))

    # 2. background stitching grid
    nvia = 0
    step = int(round(STITCH_PITCH / GRID))
    for j in range(step // 2, H, step):
        for i in range(step // 2, W, step):
            if free[j, i]:
                put_via(X0 + i * GRID, Y0 + j * GRID)
                nvia += 1
    print(f'stitching vias: {nvia}')

    # ---------- zones ----------
    pts = outline_pts(0.3)
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(b)
        z.SetLayer(layer)
        z.SetNetCode(gnd)
        z.SetAssignedPriority(0)
        z.SetLocalClearance(MM(CLEAR))
        z.SetMinThickness(MM(0.2))
        # Solid, not thermal relief: most GND pads here (WSON 0.25mm-tall pads,
        # 0402s) are too small to fit two relief spokes, and this board is
        # reflow-assembled. Solid also gives the lowest-impedance ground.
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
        z.SetThermalReliefGap(MM(0.25))
        z.SetThermalReliefSpokeWidth(MM(0.3))
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        z.SetIsFilled(False)
        v = pcbnew.VECTOR_VECTOR2I()
        for (x, y) in pts:
            v.append(pcbnew.VECTOR2I(MM(x), MM(y)))
        z.AddPolygon(v)
        b.Add(z)
    print('zones added: F.Cu, B.Cu')

    filler = pcbnew.ZONE_FILLER(b)
    filler.Fill(b.Zones())

    # 3. every F.Cu ground island must reach the B.Cu plane: find islands with
    #    no via inside and drop one in
    for attempt in range(4):
        added = 0
        vias = [(TO(t.GetPosition().x), TO(t.GetPosition().y))
                for t in b.GetTracks()
                if isinstance(t, pcbnew.PCB_VIA) and t.GetNetCode() == gnd]
        pth = [(TO(p.GetPosition().x), TO(p.GetPosition().y))
               for fp in b.GetFootprints() for p in fp.Pads()
               if p.GetNetCode() == gnd and p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH]
        anchors = vias + pth
        for z in b.Zones():
            if z.GetIsRuleArea() or z.GetLayer() != pcbnew.F_Cu:
                continue
            poly = z.GetFilledPolysList(pcbnew.F_Cu)
            for k in range(poly.OutlineCount()):
                chain = poly.COutline(k)
                bb = chain.BBox()
                lo_x, hi_x = TO(bb.GetLeft()), TO(bb.GetRight())
                lo_y, hi_y = TO(bb.GetTop()), TO(bb.GetBottom())
                if any(lo_x <= ax <= hi_x and lo_y <= ay <= hi_y and
                       chain.PointInside(pcbnew.VECTOR2I(MM(ax), MM(ay)))
                       for ax, ay in anchors):
                    continue
                spot = None
                i0, i1 = max(0, int(gx(lo_x))), min(W - 1, int(gx(hi_x)) + 1)
                j0, j1 = max(0, int(gy(lo_y))), min(H - 1, int(gy(hi_y)) + 1)
                for j in range(j0, j1 + 1):
                    for i in range(i0, i1 + 1):
                        if not free[j, i]:
                            continue
                        px, py = X0 + i * GRID, Y0 + j * GRID
                        if chain.PointInside(pcbnew.VECTOR2I(MM(px), MM(py))):
                            spot = (px, py)
                            break
                    if spot:
                        break
                if spot:
                    put_via(*spot)
                    added += 1
                else:
                    print(f'  ! island at ({lo_x:.1f},{lo_y:.1f}) has no room for a via')
        if not added:
            break
        print(f'island vias added (pass {attempt + 1}): {added}')
        filler.Fill(b.Zones())

    for z in b.Zones():
        if z.GetIsRuleArea():
            continue
        print(f'  {b.GetLayerName(z.GetLayer())}: filled={z.IsFilled()} '
              f'area={TO(TO(int(z.GetFilledArea()))):.1f} mm2')

    pcbnew.SaveBoard(PCB, b)
    print('saved')


if __name__ == '__main__':
    main()
