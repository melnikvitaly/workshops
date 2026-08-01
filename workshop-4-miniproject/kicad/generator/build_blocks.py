"""Generate the workshop-4 mini-project block diagram.

The companion to build.py: same rig, but drawn as nodes and buses instead of
symbols and pins. Boxes are graphics, not symbols — this sheet carries no
netlist and no ERC, on purpose (see "Why graphics" below). Pin-level detail
stays on miniproject-4.kicad_sch.

Not a PCB-ready design.
"""
import math
import os
import shutil
import sys

import schlib
from schlib import Sch

OUT = sys.argv[1] if len(sys.argv) > 1 else "miniproject-4-blocks.kicad_sch"

# ---------------------------------------------------------------------------
# drift guard
#
# The block sheet and the detail sheet are separate projects, so KiCad cannot
# cross-check them. Instead: every net named here must still exist in build.py,
# which is generated from the firmware. Rename a net there and this build fails
# rather than the two sheets quietly disagreeing.
# ---------------------------------------------------------------------------
NETS = ("SPI_SCK", "SPI_MOSI", "SPI_MISO", "SPI_CS0_STM", "SPI_CS3_SD",
        "I2C1_SCL", "I2C1_SDA", "I2C0_SCL", "I2C0_SDA",
        "LIGHT_A", "STM_ACT_LED", "ESP_LED", "ESP_UART1_TX")


def check_nets(path):
    if not os.path.exists(path):
        print("note: %s not found - skipping net-name check" % path)
        return
    with open(path, encoding="utf-8") as f:
        src = f.read()
    missing = [name for name in NETS if '"%s"' % name not in src]
    if missing:
        raise SystemExit(
            "net names in build_blocks.py are absent from %s: %s\n"
            "The detail sheet was renamed; update NETS and the labels below."
            % (path, ", ".join(missing)))


check_nets(os.path.join(os.path.dirname(os.path.abspath(__file__)), "build.py"))

s = Sch("Workshop 4 mini-project - system block diagram",
        rev="A", paper="A3", date="2026-08-01",
        company="Visualization only - not for PCB fabrication")
# distinct from the detail sheet's deterministic root uuid
s.uuid = schlib.uid("sheet/blocks-root")

INK = (60, 60, 60, 1)
ZONE = (30, 90, 160, 1)
MOD = (140, 100, 40, 1)
BUS = (170, 60, 40, 1)
GHOST = (150, 150, 150, 1)
FILL = (247, 247, 243, 1)


# ---------------------------------------------------------------------------
# drawing helpers
# ---------------------------------------------------------------------------
LEAD = 2.2   # baseline pitch for body text at size 1.5


def block(x1, y1, x2, y2, title, body="", color=INK, fill=FILL,
          style="solid", width=0.35, tsize=2.2, bsize=1.5):
    """A node box: framed rectangle, bold title, left-aligned body lines.

    Body lines are emitted one `text` at a time. KiCad anchors a multi-line
    text item on its vertical *centre*, so passing the block as one string
    makes it grow upward through the title.
    """
    s.rect(x1, y1, x2, y2, width=width, style=style, color=color, fill=fill)
    s.text(x1 + 3, y1 + 6.5, title, tsize, bold=True, color=color)
    for i, line in enumerate(body.split("\n") if body else []):
        if line.strip():
            s.text(x1 + 3, y1 + 12 + i * LEAD, line, bsize, color=color)


def arrow(pts, color=INK, width=0.35, style="solid", head=2.2):
    """Orthogonal polyline with a V arrowhead on the last segment."""
    s.polyline(pts, width=width, style=style, color=color)
    (ax, ay), (bx, by) = pts[-2], pts[-1]
    ang = math.atan2(by - ay, bx - ax)
    for da in (math.radians(150), math.radians(-150)):
        s.polyline([(bx, by),
                    (bx + head * math.cos(ang + da),
                     by + head * math.sin(ang + da))],
                   width=width, style="solid", color=color)


def spine(x1, x2, y, title, sub="", color=BUS, width=1.4):
    """A bus: one thick line. Anything touching it shares the same wires."""
    s.polyline([(x1, y), (x2, y)], width=width, color=color)
    s.text(x1, y - 7.5, title, 2.2, bold=True, color=color)
    if sub:
        s.text(x1, y - 3.2, sub, 1.5, color=color)


def drop(x, y1, y2, color=BUS, width=0.4, style="solid"):
    """Vertical tap from a block edge onto a bus spine."""
    s.polyline([(x, y1), (x, y2)], width=width, style=style, color=color)


# ===========================================================================
# sheet furniture
# ===========================================================================
s.text(10, 15, "Workshop 4 mini-project - system block diagram", 4.5, bold=True)
s.text(10, 21, "High-level topology only: nodes, buses and log sinks. "
                "Pin-level detail, packet format and DMA maps are on "
                "miniproject-4.kicad_sch.", 2.0)
s.text(10, 25.5, "VISUALIZATION ONLY - block diagram, no netlist. "
                 "Not intended for PCB fabrication.", 2.0, color=(170, 40, 40, 1))

s.rect(10, 32, 204, 196, width=0.5, style="dash", color=ZONE)
s.text(13, 38.5, "STM32F401 NODE  -  I2C application + SPI log slave",
       2.6, bold=True, color=ZONE)

s.rect(212, 32, 406, 196, width=0.5, style="dash", color=ZONE)
s.text(215, 38.5, "ESP32-C3 NODE  -  SPI master, collector and log sinks",
       2.6, bold=True, color=ZONE)

# ===========================================================================
# STM32F401 node
# ===========================================================================
block(18, 52, 80, 80, "Light sensor",
      "GL5528 LDR + 10k\nresistive divider")

block(96, 52, 178, 92, "STM32F401CCU6",
      "WeAct Black Pill, HSI 16 MHz\n"
      "\n"
      "ADC1_IN0, TIM2 TRGO 200 Hz\n"
      "DMA2 S0 -> 20-sample ring\n"
      "avg 10 -> LGHT @ 20 rec/s\n"
      "\n"
      "SPI1 slave, hardware NSS\n"
      "DMA2 S3 circular TX, free-run\n"
      "DMA2 S2 circular RX (debug)")

arrow([(80, 66), (96, 66)], color=MOD)
s.text(82, 63.5, "LIGHT_A", 1.4, color=MOD)

block(18, 92, 80, 114, "PC13 activity LED",
      "active low, pulsed\non SPI traffic")
arrow([(96, 84), (88, 84), (88, 103), (80, 103)], color=MOD)

# --- I2C1 ------------------------------------------------------------------
spine(34, 190, 140, "I2C1 - PB6 SCL / PB7 SDA",
      "100 kHz, blocking HAL, CPU-driven. Three addresses on one bus.")
drop(120, 92, 140)

block(30, 154, 122, 180, "Tiny RTC module",
      "DS1307 RTC - 0x68\n"
      "    +56 B NV-SRAM, CR2032 backup\n"
      "AT24C32 EEPROM - 0x50\n"
      "    4 KB, hourly light archive")
drop(68, 140, 154)

block(134, 154, 190, 176, "SSD1306 OLED",
      "0x3C, 128x64\n1 Hz status frame")
drop(162, 140, 154)

# --- SPI1 out of the zone, down to the shared bus --------------------------
s.polyline([(178, 72), (198, 72), (198, 208)], width=0.4, color=BUS)
s.text(180, 69, "SPI1 slave", 1.4, color=BUS)

# ===========================================================================
# ESP32-C3 node
# ===========================================================================
block(236, 52, 320, 92, "ESP32-C3-DevKitM-1",
      "SPI2 (FSPI) master + collector\n"
      "\n"
      "CS scan / hot-plug detect\n"
      "512-byte DMA blocks, ping-pong\n"
      "parse packets, CRC, seq / drops\n"
      "\n"
      "IO4 SCK   IO6 MOSI   IO5 MISO\n"
      "IO7 CS0 (STM)  IO3 CS3 (SD)\n"
      "33R series damping on SCK")

block(338, 52, 402, 76, "UART1 log sink",
      "IO0 TX @ 115200 8N1\nheader: TX / GND")
arrow([(320, 64), (338, 64)], color=MOD)
s.text(321, 61.5, "ESP_UART1_TX", 1.4, color=MOD)

block(338, 88, 402, 110, "IO8 activity LED",
      "active low, 15 ms\npulse per block")
arrow([(320, 84), (329, 84), (329, 99), (338, 99)], color=MOD)

# --- I2C0: up in firmware, nothing fitted ----------------------------------
drop(258, 92, 144, color=GHOST, style="dash")
s.text(260, 131, "I2C0 - IO9 / IO10, 400 kHz", 1.4, color=GHOST)
block(220, 144, 292, 168, "SSD1306 OLED - not fitted",
      "sink is compiled in, but no\ndisplay on this rig; pins spare",
      color=GHOST, fill=None, style="dash")

block(326, 140, 402, 164, "microSD card",
      "FAT32 (SDSPI + FATFS)\n"
      "datalog.txt, append + fflush\n"
      "own CS, synchronous - no DMA")

drop(302, 92, 208)
drop(364, 164, 208)

# ===========================================================================
# the shared SPI bus
# ===========================================================================
spine(40, 396, 208, "SPI BUS  -  SCK / MOSI / MISO shared, one CS per slave",
      "mode 0 (CPOL=0, CPHA=0), MSB first, 8-bit, 2 MHz")

# riser captions go below the spine: the sub-line above it already runs to x~145
s.text(200, 213, "SPI_MISO + SPI_CS0_STM", 1.4, color=BUS)
s.text(304, 213, "master - clocks the bus, CS selects the slave", 1.4, color=BUS)
s.text(366, 213, "SPI_CS3_SD", 1.4, color=BUS)

drop(96, 208, 220, color=GHOST, style="dash")
block(44, 220, 148, 242, "ESP32-S3 laser node - planned",
      "a second SPI slave on a spare CS;\nnot wired on this rig",
      color=GHOST, fill=None, style="dash")

# ===========================================================================
# notes
# ===========================================================================
s.textbox(14, 243, 212, 33, """HOW TO READ THIS SHEET
Boxes are nodes, thick lines are buses. Everything touching a bus line shares the same physical
wires and is selected by address (I2C) or by CS (SPI); slaves never talk to each other. Dashed
grey means present in firmware but not fitted on this rig.

Log stream  One-way and best effort: the master clocks 0xFF filler on MOSI, slaves present
            self-framed packets on MISO. No ack, no time sync, no command set.
Records     TIME (VT_DATETIME, 1 Hz from the DS1307) and LGHT (ADC average, 20 Hz).
Rates       The slave emits ~470 B/s; the master drains ~125 KB/s in 512-byte blocks.
Ground      Two separate builds and two separate power domains. Tie the grounds together.
CS lines    Drive every CS high straight after bus init. A floating NSS let the STM32 drive
            MISO during an SD transaction and corrupted the mount; pull-ups are the fuller fix.""",
           1.5, color=INK)

s.save(OUT)
print("wrote", OUT, os.path.getsize(OUT), "bytes")

# A .kicad_pro so the sheet opens as a project. Copied from the detail project
# once and then left alone, so KiCad's own edits to it survive a regenerate.
pro = os.path.splitext(OUT)[0] + ".kicad_pro"
src_pro = os.path.join(os.path.dirname(os.path.abspath(OUT)), "miniproject-4.kicad_pro")
if not os.path.exists(pro) and os.path.exists(src_pro):
    shutil.copyfile(src_pro, pro)
    print("wrote", pro, "(copied from miniproject-4.kicad_pro)")
