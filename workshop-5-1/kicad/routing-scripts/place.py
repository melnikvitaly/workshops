"""Board outline + placement for workshop-5-1 ESP32-S3 board."""
import pcbnew, sys

PCB = r'C:\Projects\embedded\workshops\workshop-5-1\kicad\kicad.kicad_pcb'
MM = pcbnew.FromMM
TO = pcbnew.ToMM

# board rectangle + corner radius
X0, Y0, X1, Y1, R = 20.0, 25.0, 75.0, 70.0, 2.0
HOLES = [(23.5, 28.5), (71.5, 28.5), (23.5, 66.5), (71.5, 66.5)]
HOLE_R = 1.1  # M2 clearance hole

# ref: (x, y, rotation)
PLACEMENT = {
    # --- ESP32-S3 module: antenna end overhangs the top board edge ---
    'U1':  (47.50, 31.75,   0),

    # --- region A (left of module): U1 supply entry + reset ---
    'C2':  (36.60, 27.13,  90),   # 3V3 decoupling, straddles U1 pads 1/2
    'R3':  (35.20, 30.60,   0),   # VCC -> /RESET pull-up
    'C4':  (35.20, 32.40,   0),   # /RESET -> GND
    'SW2': (28.50, 34.50,   0),   # RESET button

    # --- region B (right of module) ---
    'SW1': (66.00, 38.00,   0),   # BOOT button

    # --- USB-C inlet, bottom edge ---
    'USB1':(32.00, 66.27,   0),
    'D2':  (25.50, 62.00,  90),   # VBUS TVS
    # CC pull-downs sit outboard of the data pair so neither CC net has to
    # cross the D+/D- trunk on its way out of the connector
    'R1':  (28.30, 59.90,  90),   # CC1 5k1  (west)
    'R2':  (35.00, 59.90,  90),   # CC2 5k1  (east)
    'D5':  (30.80, 57.60,  90),   # D- TVS, on the trunk
    'D3':  (33.20, 57.60,  90),   # D+ TVS, on the trunk

    # --- +5V rail / BQ24040 charger ---
    'D4':  (40.50, 66.50, 180),   # VBUS -> +5V Schottky (A left, K right)
    'C1':  (40.00, 63.00,   0),   # 10uF bulk on +5V
    'D1':  (44.50, 66.00,   0),   # +5V TVS
    'C7':  (43.80, 63.80,  90),   # 1uF at U5 IN
    'U5':  (43.50, 60.00,   0),
    'R6':  (40.50, 57.50,   0),   # ISET
    'R13': (40.50, 55.70,   0),   # PRETERM
    'R12': (46.50, 57.50,   0),   # TS 10k
    'C8':  (46.80, 62.50, 270),   # 1uF at U5 OUT (VBAT pad toward U5)

    # --- TLV75801 LDO ---
    'C9':  (48.80, 57.05,   0),   # VBAT at U6 IN
    'U6':  (52.50, 58.00,   0),
    'C3':  (57.00, 57.05,   0),   # 10uF VCC bulk
    'C10': (56.50, 60.50,   0),   # 1uF VCC
    'R9':  (54.50, 61.00,  90),   # FB divider top
    'R10': (54.50, 63.50,  90),   # FB divider bottom (needs room for a GND via)

    # --- battery, power switch ---
    'CN1': (69.00, 48.50,   0),   # JST-XH battery, right edge
    'SW3': (64.50, 60.00,   0),   # SPDT power switch
    'R14': (68.00, 53.50,   0),   # EN pull-down

    # --- indicator LEDs along the bottom edge ---
    'R11': (44.00, 67.80,   0),
    'LED3':(47.50, 67.80, 180),   # power (VCC)
    'R7':  (51.00, 67.80,   0),
    'LED2':(54.50, 67.80, 180),   # ~PG
    'R8':  (58.00, 67.80,   0),
    'LED1':(61.50, 67.80, 180),   # ~CHG
}


def add_seg(b, x1, y1, x2, y2):
    s = pcbnew.PCB_SHAPE(b)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(pcbnew.VECTOR2I(MM(x1), MM(y1)))
    s.SetEnd(pcbnew.VECTOR2I(MM(x2), MM(y2)))
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(MM(0.05))
    b.Add(s)


def add_arc(b, sx, sy, mx, my, ex, ey):
    s = pcbnew.PCB_SHAPE(b)
    s.SetShape(pcbnew.SHAPE_T_ARC)
    s.SetArcGeometry(pcbnew.VECTOR2I(MM(sx), MM(sy)),
                     pcbnew.VECTOR2I(MM(mx), MM(my)),
                     pcbnew.VECTOR2I(MM(ex), MM(ey)))
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(MM(0.05))
    b.Add(s)


def add_circle(b, cx, cy, r):
    s = pcbnew.PCB_SHAPE(b)
    s.SetShape(pcbnew.SHAPE_T_CIRCLE)
    s.SetCenter(pcbnew.VECTOR2I(MM(cx), MM(cy)))
    s.SetStart(pcbnew.VECTOR2I(MM(cx), MM(cy)))
    s.SetEnd(pcbnew.VECTOR2I(MM(cx + r), MM(cy)))
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(MM(0.05))
    b.Add(s)


def main():
    b = pcbnew.LoadBoard(PCB)

    # wipe any existing Edge.Cuts geometry
    for d in list(b.GetDrawings()):
        if d.GetLayer() == pcbnew.Edge_Cuts:
            b.Remove(d)

    k = 1 - 0.70710678  # arc midpoint offset factor
    # straight edges
    add_seg(b, X0 + R, Y0, X1 - R, Y0)
    add_seg(b, X1, Y0 + R, X1, Y1 - R)
    add_seg(b, X1 - R, Y1, X0 + R, Y1)
    add_seg(b, X0, Y1 - R, X0, Y0 + R)
    # rounded corners
    add_arc(b, X0, Y0 + R, X0 + R * k, Y0 + R * k, X0 + R, Y0)          # TL
    add_arc(b, X1 - R, Y0, X1 - R * k, Y0 + R * k, X1, Y0 + R)          # TR
    add_arc(b, X1, Y1 - R, X1 - R * k, Y1 - R * k, X1 - R, Y1)          # BR
    add_arc(b, X0 + R, Y1, X0 + R * k, Y1 - R * k, X0, Y1 - R)          # BL
    # M2 mounting holes as Edge.Cuts circles (NPTH)
    for hx, hy in HOLES:
        add_circle(b, hx, hy, HOLE_R)

    # place footprints
    missing = []
    fps = {f.GetReference(): f for f in b.GetFootprints()}
    for ref, (x, y, rot) in PLACEMENT.items():
        fp = fps.get(ref)
        if fp is None:
            missing.append(ref)
            continue
        fp.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
        fp.SetOrientationDegrees(rot)
    unplaced = sorted(set(fps) - set(PLACEMENT))
    print('missing from board :', missing)
    print('not in placement   :', unplaced)

    pcbnew.SaveBoard(PCB, b)
    print('saved')


if __name__ == '__main__':
    main()
