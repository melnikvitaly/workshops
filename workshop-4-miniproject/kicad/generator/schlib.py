"""Tiny KiCad 10 schematic writer.

Places library symbols, computes absolute pin coordinates from the library
geometry, and emits wires / labels / graphics. No rotation support (every symbol
is placed upright, optionally mirrored) — keeps the pin transform trivial.
"""
import math
import re
import uuid as _uuid

import ksym

GRID = 1.27


def uid(seed):
    return str(_uuid.uuid5(_uuid.NAMESPACE_URL, "kicad/" + seed))


def snap(v):
    return round(round(v / GRID) * GRID, 4)


def n(v):
    """Format a number the way KiCad does (no trailing .0 noise)."""
    v = round(v, 4)
    if v == int(v):
        return str(int(v))
    return ("%.4f" % v).rstrip("0")


def esc(s):
    # KiCad's parser rejects literal newlines inside quoted strings; they have to
    # go out as \n escapes.
    return (s.replace("\\", "\\\\").replace('"', '\\"')
             .replace("\r", "").replace("\n", "\\n"))


# --------------------------------------------------------------------------
# library symbol caching / flattening
# --------------------------------------------------------------------------

PROP_RE = re.compile(r"^\t\t\(property .*?^\t\t\)\n", re.S | re.M)


def _strip_props(inner):
    return PROP_RE.sub("", inner)


def _props(text):
    return "".join(m.group(0) for m in PROP_RE.finditer(text))


def _inner(sym_text):
    """Drop the leading `(symbol "NAME"` line and the trailing `)`."""
    first_nl = sym_text.index("\n")
    body = sym_text[first_nl + 1:]
    body = body.rstrip()
    assert body.endswith(")")
    return body[:-1].rstrip("\t")


def lib_symbol_block(lib, name):
    """Self-contained `(symbol "lib:name" ...)` for the schematic cache."""
    txt, base = ksym.extract_resolved(lib, name)
    if base is None:
        inner = _inner(txt)
    else:
        base_name = ksym.extends_of(txt)
        inner = _inner(base)
        inner = _strip_props(inner) + _props(txt)
        # nested sub-symbols carry the parent's name; retarget them
        inner = inner.replace('(symbol "%s_' % base_name, '(symbol "%s_' % name)
    # re-indent from library depth (1 tab) to schematic cache depth (2 tabs)
    inner = "\n".join(("\t" + l) if l.strip() else l for l in inner.split("\n"))
    return '\t\t(symbol "%s:%s"\n%s\n\t\t)\n' % (lib, name, inner.rstrip())


# --------------------------------------------------------------------------

class Part:
    def __init__(self, ref, lib, name, x, y, mirror):
        self.ref, self.lib, self.name = ref, lib, name
        self.x, self.y, self.mirror = x, y, mirror
        txt, base = ksym.extract_resolved(lib, name)
        self.pins = ksym.pins(base if base is not None else txt)

    def _find(self, key):
        key = str(key)
        for p in self.pins:
            if p["number"] == key or p["name"] == key:
                return p
        raise KeyError("%s (%s:%s) has no pin %r" % (self.ref, self.lib, self.name, key))

    def pos(self, key):
        p = self._find(key)
        px, py = p["x"], p["y"]
        if self.mirror == "y":
            px = -px
        elif self.mirror == "x":
            py = -py
        return (round(self.x + px, 4), round(self.y - py, 4))

    def out(self, key):
        """Unit vector pointing away from the symbol body, in sheet coords."""
        p = self._find(key)
        r = math.radians(p["rot"])
        dx, dy = -math.cos(r), math.sin(r)
        if self.mirror == "y":
            dx = -dx
        elif self.mirror == "x":
            dy = -dy
        return (round(dx), round(dy))


class Sch:
    def __init__(self, title, rev="A", paper="A2", company="", date=""):
        self.title, self.rev, self.paper = title, rev, paper
        self.company, self.date = company, date
        self.libs = {}          # "lib:name" -> block text
        self.parts = {}         # ref -> Part
        self.items = []         # rendered top-level s-expressions
        self.uuid = uid("sheet/root")
        self._seq = 0

    def _u(self, kind):
        self._seq += 1
        return uid("%s/%d" % (kind, self._seq))

    # -- symbols ----------------------------------------------------------
    def sym(self, ref, libid, value, x, y, mirror=None, footprint="",
            props=None, hide_ref=False, hide_value=False, dnp=False,
            snap_pos=True, field_at=None):
        lib, name = libid.split(":", 1)
        if libid not in self.libs:
            self.libs[libid] = lib_symbol_block(lib, name)
        if snap_pos:
            x, y = snap(x), snap(y)
        else:
            x, y = round(x, 4), round(y, 4)
        part = Part(ref, lib, name, x, y, mirror)
        self.parts[ref] = part

        # Reference above Value. field_at overrides the default in-body spot for
        # symbols whose body would otherwise be written over.
        fx, fy = field_at if field_at else (x + 2.0, y - 1.5)
        fields = [("Reference", ref, hide_ref, 0.0),
                  ("Value", value, hide_value, 3.0)]
        body = []
        for pname, pval, hidden, dy in fields:
            body.append(
                '\t\t(property "%s" "%s"\n\t\t\t(at %s %s 0)\n%s'
                '\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n'
                '\t\t\t\t(justify left)\n\t\t\t)\n\t\t)\n'
                % (pname, esc(pval), n(fx), n(fy + dy),
                   "\t\t\t(hide yes)\n" if hidden else ""))
        if footprint:
            body.append(
                '\t\t(property "Footprint" "%s"\n\t\t\t(at %s %s 0)\n\t\t\t(hide yes)\n'
                '\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n'
                % (esc(footprint), n(x), n(y)))
        for k, v in (props or {}).items():
            body.append(
                '\t\t(property "%s" "%s"\n\t\t\t(at %s %s 0)\n\t\t\t(hide yes)\n'
                '\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n'
                % (esc(k), esc(v), n(x), n(y)))

        for p in part.pins:
            body.append('\t\t(pin "%s"\n\t\t\t(uuid "%s")\n\t\t)\n'
                        % (p["number"], self._u("pin")))

        mir = "\t\t(mirror %s)\n" % part.mirror if part.mirror else ""
        self.items.append(
            '\t(symbol\n\t\t(lib_id "%s")\n\t\t(at %s %s 0)\n%s'
            '\t\t(unit 1)\n\t\t(body_style 1)\n\t\t(exclude_from_sim no)\n'
            '\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(in_pos_files yes)\n'
            '\t\t(dnp %s)\n\t\t(uuid "%s")\n%s'
            '\t\t(instances\n\t\t\t(project "miniproject-4"\n\t\t\t\t(path "/%s"\n'
            '\t\t\t\t\t(reference "%s")\n\t\t\t\t\t(unit 1)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n\t)\n'
            % (libid, n(x), n(y), mir, "yes" if dnp else "no",
               self._u("sym"), "".join(body), self.uuid, ref))
        return part

    def pin(self, ref, key):
        return self.parts[ref].pos(key)

    # -- connectivity -----------------------------------------------------
    def wire(self, *pts):
        pts = [(round(a, 4), round(b, 4)) for a, b in pts]
        for a, b in zip(pts, pts[1:]):
            if a == b:
                continue
            self.items.append(
                '\t(wire\n\t\t(pts\n\t\t\t(xy %s %s) (xy %s %s)\n\t\t)\n'
                '\t\t(stroke\n\t\t\t(width 0)\n\t\t\t(type default)\n\t\t)\n'
                '\t\t(uuid "%s")\n\t)\n'
                % (n(a[0]), n(a[1]), n(b[0]), n(b[1]), self._u("wire")))

    def junction(self, x, y):
        self.items.append(
            '\t(junction\n\t\t(at %s %s)\n\t\t(diameter 0)\n'
            '\t\t(color 0 0 0 0)\n\t\t(uuid "%s")\n\t)\n'
            % (n(x), n(y), self._u("j")))

    def no_connect(self, x, y):
        self.items.append('\t(no_connect\n\t\t(at %s %s)\n\t\t(uuid "%s")\n\t)\n'
                          % (n(x), n(y), self._u("nc")))

    def glabel(self, x, y, name, angle=0, shape="bidirectional", size=1.27):
        just = "left" if angle in (0, 90) else "right"
        self.items.append(
            '\t(global_label "%s"\n\t\t(shape %s)\n\t\t(at %s %s %d)\n'
            '\t\t(effects\n\t\t\t(font\n\t\t\t\t(size %s %s)\n\t\t\t)\n'
            '\t\t\t(justify %s)\n\t\t)\n\t\t(uuid "%s")\n'
            '\t\t(property "Intersheetrefs" "${INTERSHEET_REFS}"\n\t\t\t(at %s %s 0)\n'
            '\t\t\t(hide yes)\n\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n'
            '\t\t\t\t)\n\t\t\t)\n\t\t)\n\t)\n'
            % (esc(name), shape, n(x), n(y), angle, n(size), n(size), just,
               self._u("gl"), n(x), n(y)))

    def label(self, x, y, name, angle=0, size=1.27):
        just = "left" if angle in (0, 90) else "right"
        self.items.append(
            '\t(label "%s"\n\t\t(at %s %s %d)\n\t\t(effects\n\t\t\t(font\n'
            '\t\t\t\t(size %s %s)\n\t\t\t)\n\t\t\t(justify %s bottom)\n\t\t)\n'
            '\t\t(uuid "%s")\n\t)\n'
            % (esc(name), n(x), n(y), angle, n(size), n(size), just, self._u("lbl")))

    # -- stubs: wire outward from a pin, optionally terminate in a label ---
    def stub(self, ref, key, length, label=None, shape="bidirectional", size=1.27):
        part = self.parts[ref]
        px, py = part.pos(key)
        dx, dy = part.out(key)
        ex, ey = px + dx * length, py + dy * length
        self.wire((px, py), (ex, ey))
        if label:
            angle = 0 if dx > 0 else 180 if dx < 0 else (90 if dy < 0 else 270)
            self.glabel(ex, ey, label, angle, shape, size)
        return (ex, ey)

    def power(self, ref, kind, x, y):
        """Place a power symbol so its pin sits exactly at (x, y).

        Never snapped: the caller passes a wire end derived from pin geometry,
        and rounding it to the grid would silently break the connection.
        """
        return self.sym(ref, "power:" + kind, kind, x, y,
                        hide_ref=True, hide_value=True, snap_pos=False)

    def pwr_stub(self, ref, key, length, kind, pref):
        end = self.stub(ref, key, length)
        self.power(pref, kind, end[0], end[1])
        return end

    # -- graphics ---------------------------------------------------------
    def rect(self, x1, y1, x2, y2, width=0.4, style="dash", color=(80, 80, 80, 1),
             fill=None):
        f = ('\t\t(fill\n\t\t\t(type color)\n\t\t\t(color %d %d %d %s)\n\t\t)\n'
             % fill) if fill else '\t\t(fill\n\t\t\t(type none)\n\t\t)\n'
        self.items.append(
            '\t(rectangle\n\t\t(start %s %s)\n\t\t(end %s %s)\n'
            '\t\t(stroke\n\t\t\t(width %s)\n\t\t\t(type %s)\n'
            '\t\t\t(color %d %d %d %s)\n\t\t)\n%s\t\t(uuid "%s")\n\t)\n'
            % (n(x1), n(y1), n(x2), n(y2), n(width), style,
               color[0], color[1], color[2], n(color[3]), f, self._u("rect")))

    def polyline(self, pts, width=0.3, style="solid", color=(80, 80, 80, 1)):
        body = " ".join("(xy %s %s)" % (n(a), n(b)) for a, b in pts)
        self.items.append(
            '\t(polyline\n\t\t(pts\n\t\t\t%s\n\t\t)\n'
            '\t\t(stroke\n\t\t\t(width %s)\n\t\t\t(type %s)\n'
            '\t\t\t(color %d %d %d %s)\n\t\t)\n\t\t(uuid "%s")\n\t)\n'
            % (body, n(width), style, color[0], color[1], color[2], n(color[3]),
               self._u("poly")))

    def text(self, x, y, s, size=1.27, bold=False, color=None, justify="left bottom"):
        col = ('\t\t\t\t(color %d %d %d %s)\n' % (color[0], color[1], color[2],
                                                  n(color[3]))) if color else ""
        self.items.append(
            '\t(text "%s"\n\t\t(exclude_from_sim yes)\n\t\t(at %s %s 0)\n'
            '\t\t(effects\n\t\t\t(font\n\t\t\t\t(size %s %s)\n%s%s\t\t\t)\n'
            '\t\t\t(justify %s)\n\t\t)\n\t\t(uuid "%s")\n\t)\n'
            % (esc(s), n(x), n(y), n(size), n(size), col,
               "\t\t\t\t(bold yes)\n" if bold else "", justify, self._u("txt")))

    def textbox(self, x, y, w, h, s, size=1.27, border=True,
                color=(90, 90, 90, 1), fill=None, bold=False):
        f = ('\t\t(fill\n\t\t\t(type color)\n\t\t\t(color %d %d %d %s)\n\t\t)\n'
             % fill) if fill else '\t\t(fill\n\t\t\t(type none)\n\t\t)\n'
        stroke = ('\t\t(stroke\n\t\t\t(width 0.2)\n\t\t\t(type solid)\n'
                  '\t\t\t(color %d %d %d %s)\n\t\t)\n'
                  % (color[0], color[1], color[2], n(color[3]))) if border else \
                 '\t\t(stroke\n\t\t\t(width 0)\n\t\t\t(type none)\n\t\t)\n'
        self.items.append(
            '\t(text_box "%s"\n\t\t(exclude_from_sim yes)\n\t\t(at %s %s 0)\n'
            '\t\t(size %s %s)\n\t\t(margins 1.27 1.27 1.27 1.27)\n%s%s'
            '\t\t(effects\n\t\t\t(font\n\t\t\t\t(size %s %s)\n%s\t\t\t)\n'
            '\t\t\t(justify left top)\n\t\t)\n\t\t(uuid "%s")\n\t)\n'
            % (esc(s), n(x), n(y), n(w), n(h), stroke, f, n(size), n(size),
               "\t\t\t\t(bold yes)\n" if bold else "", self._u("tb")))

    # -- output -----------------------------------------------------------
    def dumps(self):
        head = ('(kicad_sch\n\t(version 20260306)\n\t(generator "eeschema")\n'
                '\t(generator_version "10.0")\n\t(uuid "%s")\n\t(paper "%s")\n'
                '\t(title_block\n\t\t(title "%s")\n\t\t(date "%s")\n\t\t(rev "%s")\n'
                '\t\t(company "%s")\n\t)\n'
                % (self.uuid, self.paper, esc(self.title), esc(self.date),
                   esc(self.rev), esc(self.company)))
        libs = "\t(lib_symbols\n" + "".join(
            self.libs[k] for k in sorted(self.libs)) + "\t)\n"
        tail = ('\t(sheet_instances\n\t\t(path "/"\n\t\t\t(page "1")\n\t\t)\n\t)\n'
                '\t(embedded_fonts no)\n)\n')
        return head + libs + "".join(self.items) + tail

    def save(self, path):
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            f.write(self.dumps())
