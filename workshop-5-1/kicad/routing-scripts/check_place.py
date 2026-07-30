"""Courtyard-overlap + board-containment check."""
import pcbnew, itertools

PCB = r'C:\Projects\embedded\workshops\workshop-5-1\kicad\kicad.kicad_pcb'
TO = pcbnew.ToMM
X0, Y0, X1, Y1 = 20.0, 25.0, 75.0, 70.0
HOLES = [(23.5, 28.5), (71.5, 28.5), (23.5, 66.5), (71.5, 66.5)]

b = pcbnew.LoadBoard(PCB)
fps = list(b.GetFootprints())

# --- courtyard overlaps (uses real polygons, not bboxes) ---
print('=== courtyard overlaps ===')
n = 0
for a, c in itertools.combinations(fps, 2):
    pa = a.GetCourtyard(pcbnew.F_CrtYd)
    pc = c.GetCourtyard(pcbnew.F_CrtYd)
    if pa.OutlineCount() == 0 or pc.OutlineCount() == 0:
        continue
    inter = pcbnew.SHAPE_POLY_SET(pa)
    inter.BooleanIntersection(pc)
    if inter.OutlineCount() and inter.Area() > pcbnew.FromMM(0.001) ** 2:
        bb = inter.BBox()
        print(f'  {a.GetReference():5s} <-> {c.GetReference():5s}  '
              f'x[{TO(bb.GetLeft()):.2f},{TO(bb.GetRight()):.2f}] '
              f'y[{TO(bb.GetTop()):.2f},{TO(bb.GetBottom()):.2f}]')
        n += 1
print(f'  total: {n}')

# --- pads outside board / too close to edge or holes ---
print('=== pad vs board edge (min 0.3mm) ===')
m = 0
for f in fps:
    for p in f.Pads():
        c = p.GetPosition(); s = p.GetSize()
        L, Rr = TO(c.x - s.x / 2), TO(c.x + s.x / 2)
        T, B = TO(c.y - s.y / 2), TO(c.y + s.y / 2)
        d = min(L - X0, X1 - Rr, T - Y0, Y1 - B)
        bad = d < 0.3
        for hx, hy in HOLES:
            cx, cy = TO(c.x), TO(c.y)
            dd = ((max(abs(cx - hx) - TO(s.x) / 2, 0)) ** 2 +
                  (max(abs(cy - hy) - TO(s.y) / 2, 0)) ** 2) ** 0.5 - 1.1
            if dd < 0.3:
                bad = True; d = min(d, dd)
        if bad:
            print(f'  {f.GetReference()}.{p.GetNumber()} clearance {d:.2f}mm')
            m += 1
print(f'  total: {m}')

# --- courtyard extent vs board ---
print('=== courtyard outside board ===')
for f in fps:
    bb = f.GetCourtyard(pcbnew.F_CrtYd).BBox()
    L, Rr, T, B = TO(bb.GetLeft()), TO(bb.GetRight()), TO(bb.GetTop()), TO(bb.GetBottom())
    if L < X0 - .05 or Rr > X1 + .05 or T < Y0 - .05 or B > Y1 + .05:
        print(f'  {f.GetReference():5s} x[{L:.2f},{Rr:.2f}] y[{T:.2f},{B:.2f}]')
