"""Minimal KiCad .kicad_sym reader: extract a symbol's raw s-expression text
and its pin geometry (name, number, x, y, rotation)."""
import re, sys, os, glob

SYMDIR = os.path.expandvars(r"%LOCALAPPDATA%\Programs\KiCad\10.0\share\kicad\symbols")


def read_lib(lib):
    with open(os.path.join(SYMDIR, lib + ".kicad_sym"), encoding="utf-8") as f:
        return f.read()


def _match_paren(text, start):
    """start = index of '('; return index just past the matching ')'."""
    depth = 0
    i = start
    in_str = False
    while i < len(text):
        c = text[i]
        if in_str:
            if c == '\\':
                i += 2
                continue
            if c == '"':
                in_str = False
        elif c == '"':
            in_str = True
        elif c == '(':
            depth += 1
        elif c == ')':
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError("unbalanced")


def extract(lib, name):
    text = read_lib(lib)
    needle = '\n\t(symbol "%s"\n' % name
    idx = text.find(needle)
    if idx < 0:
        raise KeyError("%s:%s not found" % (lib, name))
    start = idx + 1  # the '('
    end = _match_paren(text, start)
    return text[start:end]


def extends_of(sym_text):
    m = re.search(r'\(extends "([^"]+)"\)', sym_text)
    return m.group(1) if m else None


def extract_resolved(lib, name):
    """Symbol text plus the base it extends (pins live in the base)."""
    txt = extract(lib, name)
    base = extends_of(txt)
    return txt, (extract(lib, base) if base else None)


PIN_RE = re.compile(
    r'\(pin\s+(\w+)\s+(\w+)\s*\n\s*\(at\s+([-\d.]+)\s+([-\d.]+)\s+([-\d.]+)\)\s*\n'
    r'\s*\(length\s+([-\d.]+)\)(.*?)\(name\s+"([^"]*)".*?\(number\s+"([^"]*)"',
    re.S)


def pins(sym_text):
    out = []
    for m in PIN_RE.finditer(sym_text):
        out.append(dict(etype=m.group(1), shape=m.group(2),
                        x=float(m.group(3)), y=float(m.group(4)),
                        rot=float(m.group(5)), length=float(m.group(6)),
                        name=m.group(8), number=m.group(9)))
    return out


def find_lib(name):
    for p in glob.glob(os.path.join(SYMDIR, "*.kicad_sym")):
        with open(p, encoding="utf-8") as f:
            if '\t(symbol "%s"' % name in f.read():
                return os.path.splitext(os.path.basename(p))[0]
    return None


if __name__ == "__main__":
    for spec in sys.argv[1:]:
        if ":" in spec:
            lib, name = spec.split(":", 1)
        else:
            name = spec
            lib = find_lib(name)
        txt, base = extract_resolved(lib, name)
        print("### %s:%s%s" % (lib, name,
                               "  (extends %s)" % extends_of(txt) if base else ""))
        for p in pins(base if base else txt):
            print("   %-5s %-14s (%7.2f,%7.2f) r%-4.0f %s" % (
                p["number"], p["name"], p["x"], p["y"], p["rot"], p["etype"]))
