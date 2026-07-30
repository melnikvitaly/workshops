"""Grid maze router (A*, 2 layers, via-aware) with power-net neck-down."""
import heapq, math
import numpy as np
import pcbnew

PCB = r'C:\Projects\embedded\workshops\workshop-5-1\kicad\kicad.kicad_pcb'
MM, TO = pcbnew.FromMM, pcbnew.ToMM

GRID = 0.05
X0, Y0, X1, Y1 = 20.0, 25.0, 75.0, 70.0
CORNER_R = 2.0
HOLES = [(23.5, 28.5), (71.5, 28.5), (23.5, 66.5), (71.5, 66.5)]
HOLE_R = 1.1

W = int(round((X1 - X0) / GRID)) + 1
H = int(round((Y1 - Y0) / GRID)) + 1

CLEAR = 0.2
EDGE_CLEAR = 0.5
# A straight step parallel to an obstacle face keeps its clearance exactly, but a
# diagonal step can dip below it between the two cell centres (by at most half a
# step). Diagonals are therefore tested against a slightly grown mask; straight
# escapes out of 0.5mm-pitch pads keep the exact rule and stay routable.
DIAG_MARGIN = 0.045
VIA_MARGIN = 0.03
VIA_D, VIA_DRILL = 0.6, 0.3
EPS = 1e-7

POURED = {'GND'}

# width used while routing: must be able to escape the finest pad on the net
ESCAPE_W = {'+5V': 0.30, 'VBAT': 0.30, 'Net-(D2-A1)': 0.35, 'VCC': 0.40}
# width the segment is widened to wherever clearance allows (neck-down)
WIDE_W = {'+5V': 0.60, 'VBAT': 0.60, 'Net-(D2-A1)': 0.60, 'VCC': 0.60}
DEFAULT_W = 0.2

LAYERS = [pcbnew.F_Cu, pcbnew.B_Cu]
LAYER_STEP_COST = [10, 13]      # keep B.Cu ground plane as intact as possible
VIA_COST = 300

ORTHO = [(1, 0), (-1, 0), (0, 1), (0, -1)]
DIAG = [(1, 1), (1, -1), (-1, 1), (-1, -1)]


def gx(x): return (x - X0) / GRID
def gy(y): return (y - Y0) / GRID
def mx(i): return X0 + i * GRID
def my(j): return Y0 + j * GRID


def strict_range(lo, hi, n):
    """indices of grid lines strictly inside (lo, hi), clamped to [0, n-1]"""
    a = int(math.ceil(lo + EPS))
    bnd = int(math.floor(hi - EPS))
    return max(a, 0), min(bnd, n - 1)


def rect_cells(x_lo, y_lo, x_hi, y_hi):
    i0, i1 = strict_range(gx(x_lo), gx(x_hi), W)
    j0, j1 = strict_range(gy(y_lo), gy(y_hi), H)
    if i1 < i0 or j1 < j0:
        return None
    return j0, j1 + 1, i0, i1 + 1


def disc_cells(cx, cy, r):
    sl = rect_cells(cx - r, cy - r, cx + r, cy + r)
    if not sl:
        return None
    j0, j1, i0, i1 = sl
    ii = np.arange(i0, i1) * GRID + X0 - cx
    jj = np.arange(j0, j1) * GRID + Y0 - cy
    d2 = ii[None, :] ** 2 + jj[:, None] ** 2
    return j0, j1, i0, i1, d2 < r * r - EPS


class Router:
    def __init__(self, board):
        self.b = board
        self.pads = []
        for fp in board.GetFootprints():
            for p in fp.Pads():
                bb = p.GetBoundingBox()
                c = p.GetPosition()
                self.pads.append(dict(
                    net=p.GetNetname(), ref=fp.GetReference(), num=p.GetNumber(),
                    pth=p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH,
                    lm=(p.IsOnLayer(pcbnew.F_Cu), p.IsOnLayer(pcbnew.B_Cu)),
                    xlo=TO(bb.GetLeft()), ylo=TO(bb.GetTop()),
                    xhi=TO(bb.GetRight()), yhi=TO(bb.GetBottom()),
                    cx=TO(c.x), cy=TO(c.y)))
        self.tracks = []      # (layer, x1,y1,x2,y2, width, net) - emitted by us
        self.vias = []        # (x, y, net)
        # copper already on the board (pre-placed GND stitching vias): obstacles
        # for routing, but never re-emitted
        self.fixed_tracks, self.fixed_vias = [], []
        for t in board.GetTracks():
            nm = t.GetNetname()
            s = t.GetStart()
            if isinstance(t, pcbnew.PCB_VIA):
                self.fixed_vias.append((TO(s.x), TO(s.y), nm))
            else:
                e = t.GetEnd()
                L = 0 if t.IsOnLayer(pcbnew.F_Cu) else 1
                self.fixed_tracks.append((L, TO(s.x), TO(s.y), TO(e.x), TO(e.y),
                                          TO(t.GetWidth()), nm))

    # ---------------- obstacles ----------------
    def build_mask(self, net, w, extra=0.0):
        half = w / 2.0 + extra
        blocked = np.zeros((2, H, W), dtype=bool)
        m = EDGE_CLEAR + half

        i0, i1 = strict_range(-1e9, gx(X0 + m), W)
        blocked[:, :, i0:i1 + 1] = True
        i0, i1 = strict_range(gx(X1 - m), 1e9, W)
        blocked[:, :, i0:i1 + 1] = True
        j0, j1 = strict_range(-1e9, gy(Y0 + m), H)
        blocked[:, j0:j1 + 1, :] = True
        j0, j1 = strict_range(gy(Y1 - m), 1e9, H)
        blocked[:, j0:j1 + 1, :] = True

        for (ccx, ccy, sx, sy) in [(X0 + CORNER_R, Y0 + CORNER_R, -1, -1),
                                   (X1 - CORNER_R, Y0 + CORNER_R, 1, -1),
                                   (X1 - CORNER_R, Y1 - CORNER_R, 1, 1),
                                   (X0 + CORNER_R, Y1 - CORNER_R, -1, 1)]:
            r = CORNER_R - m
            sl = rect_cells(min(ccx, ccx + sx * CORNER_R) - .1, min(ccy, ccy + sy * CORNER_R) - .1,
                            max(ccx, ccx + sx * CORNER_R) + .1, max(ccy, ccy + sy * CORNER_R) + .1)
            if not sl:
                continue
            j0, j1, i0, i1 = sl
            ii = np.arange(i0, i1) * GRID + X0 - ccx
            jj = np.arange(j0, j1) * GRID + Y0 - ccy
            out = (ii[None, :] ** 2 + jj[:, None] ** 2) > r * r
            blocked[0, j0:j1, i0:i1] |= out
            blocked[1, j0:j1, i0:i1] |= out

        for hx, hy in HOLES:
            d = disc_cells(hx, hy, HOLE_R + EDGE_CLEAR + half)
            if d:
                j0, j1, i0, i1, msk = d
                blocked[0, j0:j1, i0:i1] |= msk
                blocked[1, j0:j1, i0:i1] |= msk

        infl = CLEAR + half
        for p in self.pads:
            if p['net'] == net:
                continue
            sl = rect_cells(p['xlo'] - infl, p['ylo'] - infl, p['xhi'] + infl, p['yhi'] + infl)
            if not sl:
                continue
            j0, j1, i0, i1 = sl
            if p['lm'][0]:
                blocked[0, j0:j1, i0:i1] = True
            if p['lm'][1]:
                blocked[1, j0:j1, i0:i1] = True

        # Reserve a via landing site on B.Cu beside every SMD ground pad. Without
        # this the router can run a back-layer trace under a ground pad and seal
        # its F.Cu island off from the plane, leaving that pad unconnected.
        for p in self.pads:
            if p['net'] != 'GND' or p['pth']:
                continue
            hd = math.hypot(p['xhi'] - p['xlo'], p['yhi'] - p['ylo']) / 2
            r = hd + VIA_D / 2 + CLEAR
            d = disc_cells(p['cx'], p['cy'], r)
            if d:
                j0, j1, i0, i1, msk = d
                blocked[1, j0:j1, i0:i1] |= msk

        for (L, x1, y1, x2, y2, tw, tnet) in self.fixed_tracks + self.tracks:
            if tnet == net:
                continue
            self._stamp(blocked[L], x1, y1, x2, y2, tw / 2 + infl)
        for (vx, vy, vnet) in self.fixed_vias + self.vias:
            if vnet == net:
                continue
            d = disc_cells(vx, vy, VIA_D / 2 + infl)
            if d:
                j0, j1, i0, i1, msk = d
                blocked[0, j0:j1, i0:i1] |= msk
                blocked[1, j0:j1, i0:i1] |= msk
        return blocked

    @staticmethod
    def _stamp(grid, x1, y1, x2, y2, r):
        n = max(1, int(math.hypot(x2 - x1, y2 - y1) / (GRID * 0.7)) + 1)
        for k in range(n + 1):
            t = k / n
            d = disc_cells(x1 + (x2 - x1) * t, y1 + (y2 - y1) * t, r)
            if d:
                j0, j1, i0, i1, msk = d
                grid[j0:j1, i0:i1] |= msk

    # ---------------- pad footprints on the grid ----------------
    def pad_cells(self, p, w):
        sh = w / 2
        xl, xh = p['xlo'] + sh, p['xhi'] - sh
        yl, yh = p['ylo'] + sh, p['yhi'] - sh
        if xh <= xl:
            xl = xh = p['cx']
        if yh <= yl:
            yl = yh = p['cy']
        i0, i1 = strict_range(gx(xl) - EPS, gx(xh) + EPS, W)
        j0, j1 = strict_range(gy(yl) - EPS, gy(yh) + EPS, H)
        if i1 < i0:
            i0 = i1 = int(round(gx(p['cx'])))
        if j1 < j0:
            j0 = j1 = int(round(gy(p['cy'])))
        out = set()
        for L in range(2):
            if not p['lm'][L]:
                continue
            for j in range(j0, j1 + 1):
                for i in range(i0, i1 + 1):
                    out.add((L, j, i))
        return out

    # ---------------- search ----------------
    @staticmethod
    def h(j, i, tgt):
        best = 1 << 60
        for (tj, ti, slack) in tgt:
            dx = abs(ti - i); dy = abs(tj - j)
            d = (max(dx, dy) - min(dx, dy)) * 10 + min(dx, dy) * 14 - slack
            if d < best:
                best = d
        return best if best > 0 else 0

    def astar(self, blocked, sources, targets, tgt_pts, via_ok=None, bdiag=None):
        dist, prev, pq = {}, {}, []
        for s in sources:
            L, j, i = s
            if blocked[L, j, i]:
                continue
            dist[s] = 0
            heapq.heappush(pq, (self.h(j, i, tgt_pts), 0, s))
        if not pq:
            return None
        while pq:
            _, g, cur = heapq.heappop(pq)
            if g > dist.get(cur, 1 << 60):
                continue
            if cur in targets:
                path = [cur]
                while cur in prev:
                    cur = prev[cur]
                    path.append(cur)
                return path[::-1]
            L, j, i = cur
            base = LAYER_STEP_COST[L]
            dcost = int(base * 1.414)
            diag_here = bdiag is None or not bdiag[L, j, i]
            for dx, dy, c in ([(d[0], d[1], base) for d in ORTHO] +
                              [(d[0], d[1], dcost) for d in DIAG]):
                ni, nj = i + dx, j + dy
                if not (0 <= ni < W and 0 <= nj < H) or blocked[L, nj, ni]:
                    continue
                if dx and dy:
                    if blocked[L, j, ni] or blocked[L, nj, i]:
                        continue          # no corner cutting
                    if not diag_here or (bdiag is not None and bdiag[L, nj, ni]):
                        continue          # diagonal needs the grown clearance
                nxt = (L, nj, ni)
                ng = g + c
                if ng < dist.get(nxt, 1 << 60):
                    dist[nxt] = ng
                    prev[nxt] = cur
                    heapq.heappush(pq, (ng + self.h(nj, ni, tgt_pts), ng, nxt))
            nl = 1 - L
            # a via is 0.6mm of copper plus a drill - far bigger than the track,
            # so it needs its own legality check on both layers
            if not blocked[nl, j, i] and (via_ok is None or
                                          (via_ok[0, j, i] and via_ok[1, j, i])):
                nxt = (nl, j, i)
                ng = g + VIA_COST
                if ng < dist.get(nxt, 1 << 60):
                    dist[nxt] = ng
                    prev[nxt] = cur
                    heapq.heappush(pq, (ng + self.h(j, i, tgt_pts), ng, nxt))
        return None

    # ---------------- per-net ----------------
    def route_net(self, net):
        w = ESCAPE_W.get(net, DEFAULT_W)
        pads = [p for p in self.pads if p['net'] == net]
        uniq = []
        for p in pads:
            if not any(abs(p['cx'] - q['cx']) < 1e-6 and abs(p['cy'] - q['cy']) < 1e-6
                       for q in uniq):
                uniq.append(p)
        pads = uniq
        if len(pads) < 2:
            return True, f'{len(pads)} unique pad(s), nothing to route'

        blocked = self.build_mask(net, w)
        bdiag = self.build_mask(net, w, DIAG_MARGIN)
        via_ok = ~self.build_mask(net, VIA_D, VIA_MARGIN)
        cells = {id(p): self.pad_cells(p, w) for p in pads}
        src = set(cells[id(pads[0])])
        remaining = pads[1:]
        while remaining:
            tgt, tgt_pts = set(), []
            for p in remaining:
                tgt |= cells[id(p)]
                slack = int(max(p['xhi'] - p['xlo'], p['yhi'] - p['ylo']) / 2 / GRID * 14)
                tgt_pts.append((int(round(gy(p['cy']))), int(round(gx(p['cx']))), slack))
            path = self.astar(blocked, src, tgt, tgt_pts, via_ok, bdiag)
            if path is None:
                names = ', '.join(f'{p["ref"]}.{p["num"]}' for p in remaining)
                return False, f'unreachable: {names}'
            self.commit(path, net, w)
            end = path[-1]
            hit = next(p for p in remaining if end in cells[id(p)])
            remaining.remove(hit)
            src |= set(path) | cells[id(hit)]
        return True, f'{len(pads)} pads'

    def commit(self, path, net, w):
        runs, cur = [], [path[0]]
        for a, b in zip(path, path[1:]):
            if a[0] != b[0]:
                runs.append(cur)
                self.vias.append((mx(a[2]), my(a[1]), net))
                cur = [b]
            else:
                cur.append(b)
        runs.append(cur)
        for cells in runs:
            if len(cells) < 2:
                continue
            L = cells[0][0]
            pts = [(mx(i), my(j)) for (_, j, i) in cells]
            simp = [pts[0]]
            for k in range(1, len(pts) - 1):
                ax, ay = simp[-1]; bx, by = pts[k]; cx, cy = pts[k + 1]
                if abs((bx - ax) * (cy - ay) - (by - ay) * (cx - ax)) > 1e-9:
                    simp.append(pts[k])
            simp.append(pts[-1])
            for p, q in zip(simp, simp[1:]):
                self.tracks.append((L, p[0], p[1], q[0], q[1], w, net))

    # ---------------- neck-down widening ----------------
    def widen(self, net):
        target = WIDE_W[net]
        blocked = self.build_mask(net, target, DIAG_MARGIN)
        out, changed = [], 0.0
        for t in self.tracks:
            L, x1, y1, x2, y2, w, tn = t
            if tn != net or w >= target:
                out.append(t)
                continue
            ln = math.hypot(x2 - x1, y2 - y1)
            n = max(1, int(ln / (GRID / 2)))
            ok = []
            for k in range(n + 1):
                s = k / n
                i = int(round(gx(x1 + (x2 - x1) * s)))
                j = int(round(gy(y1 + (y2 - y1) * s)))
                ok.append(0 <= i < W and 0 <= j < H and not blocked[L, j, i])
            # collapse short runs so the width doesn't stipple
            minrun = max(2, int(0.5 / (ln / n))) if ln else 2
            k = 0
            runs = []
            while k <= n:
                v = ok[k]; s = k
                while k <= n and ok[k] == v:
                    k += 1
                runs.append([v, s, k - 1])
            for r in runs:
                if r[0] and (r[2] - r[1]) < minrun:
                    r[0] = False
            merged = []
            for r in runs:
                if merged and merged[-1][0] == r[0]:
                    merged[-1][2] = r[2]
                else:
                    merged.append(r)
            for v, a, bnd in merged:
                sa, sb = a / n, bnd / n
                ax, ay = x1 + (x2 - x1) * sa, y1 + (y2 - y1) * sa
                bx, by = x1 + (x2 - x1) * sb, y1 + (y2 - y1) * sb
                if abs(bx - ax) < 1e-9 and abs(by - ay) < 1e-9:
                    continue
                out.append((L, ax, ay, bx, by, target if v else w, tn))
                if v:
                    changed += math.hypot(bx - ax, by - ay)
            # stitch gaps left by sampling
        self.tracks = out
        return changed

    # ---------------- emit ----------------
    def apply(self):
        ni = self.b.GetNetInfo()
        code = {}
        for i in range(self.b.GetNetCount()):
            n = ni.GetNetItem(i)
            if n:
                code[n.GetNetname()] = n.GetNetCode()
        for (L, x1, y1, x2, y2, w, net) in self.tracks:
            t = pcbnew.PCB_TRACK(self.b)
            t.SetStart(pcbnew.VECTOR2I(MM(x1), MM(y1)))
            t.SetEnd(pcbnew.VECTOR2I(MM(x2), MM(y2)))
            t.SetWidth(MM(w))
            t.SetLayer(LAYERS[L])
            t.SetNetCode(code[net])
            self.b.Add(t)
        for (x, y, net) in self.vias:
            v = pcbnew.PCB_VIA(self.b)
            v.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
            v.SetWidth(MM(VIA_D))
            v.SetDrill(MM(VIA_DRILL))
            v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
            v.SetNetCode(code[net])
            self.b.Add(v)


def main():
    b = pcbnew.LoadBoard(PCB)
    # clear previous routing but keep the pre-placed GND stitching vias
    for t in list(b.GetTracks()):
        if not (isinstance(t, pcbnew.PCB_VIA) and t.GetNetname() == 'GND'):
            b.Remove(t)

    r = Router(b)
    nets = {}
    for p in r.pads:
        n = p['net']
        if not n or n.startswith('unconnected-') or n in POURED:
            continue
        nets.setdefault(n, []).append(p)

    def span(n):
        ps = nets[n]
        return (max(p['cx'] for p in ps) - min(p['cx'] for p in ps) +
                max(p['cy'] for p in ps) - min(p['cy'] for p in ps))

    # USB-C escapes are the most congested: claim them before anything else
    first = ['/D-', '/D+', 'Net-(D2-A1)', 'Net-(USB1-CC1)', 'Net-(USB1-CC2)']
    order = [n for n in first if n in nets] + sorted(
        [n for n in nets if n not in first], key=span)

    fail = []
    for n in order:
        good, msg = r.route_net(n)
        print(f'{"OK  " if good else "FAIL"}  {n:20s} {msg}', flush=True)
        if not good:
            fail.append(n)

    print(f'\nrouted {len(order) - len(fail)}/{len(order)} nets, '
          f'{len(r.tracks)} segments, {len(r.vias)} vias')
    if fail:
        print('FAILED:', fail)

    for n in WIDE_W:
        if n in nets and n not in fail:
            d = r.widen(n)
            print(f'widened {n:14s} {d:6.1f} mm to {WIDE_W[n]} mm')

    r.apply()
    pcbnew.SaveBoard(PCB, b)
    print('saved')


if __name__ == '__main__':
    main()
