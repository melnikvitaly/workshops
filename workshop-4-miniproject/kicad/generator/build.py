"""Generate the workshop-4 mini-project interconnect schematic.

Visualization only: the two firmware projects (espc3/ ESP32-C3 SPI master and
stm/miniproject-4/ STM32F401 sensor node + SPI log slave) drawn as one sheet,
with the protocol / DMA behaviour annotated. Not a PCB-ready design.
"""
import os
import sys

import schlib
from schlib import Sch

OUT = sys.argv[1] if len(sys.argv) > 1 else "miniproject-4.kicad_sch"

s = Sch("Workshop 4 mini-project - ESP32-C3 log collector + STM32F401 sensor node",
        rev="A", paper="A2", date="2026-08-01",
        company="Visualization only - not for PCB fabrication")

INK = (60, 60, 60, 1)
ZONE = (30, 90, 160, 1)
MOD = (140, 100, 40, 1)


def route(a, b, mode="hv", mid=None):
    """Orthogonal path between two points."""
    if mode == "hv":
        return [a, (b[0], a[1]), b]
    if mode == "vh":
        return [a, (a[0], b[1]), b]
    if mode == "hvh":
        return [a, (mid, a[1]), (mid, b[1]), b]
    if mode == "vhv":
        return [a, (a[0], mid), (b[0], mid), b]
    raise ValueError(mode)


def drop(ref, key, run, rise, kind, pref):
    """Stub a pin outward `run` mm, jog `rise` mm (+down/-up), land a power symbol."""
    end = s.stub(ref, key, run)
    tgt = (end[0], end[1] + rise)
    s.wire(end, tgt)
    s.power(pref, kind, tgt[0], tgt[1])
    return tgt


def rail(pinrefs, rail_y, kind, pref, tap_dx=-6.35, tap_dy=-5.08):
    """Tie several same-net pins to a horizontal rail and one power symbol."""
    xs = []
    for ref, key in pinrefs:
        p = s.pin(ref, key)
        s.wire(p, (p[0], rail_y))
        xs.append(p[0])
    xs = sorted(set(xs))
    tap_x = xs[0] + tap_dx
    s.wire((tap_x, rail_y), (xs[-1], rail_y))
    for x in xs:
        s.junction(x, rail_y)
    s.wire((tap_x, rail_y), (tap_x, rail_y + tap_dy))
    s.power(pref, kind, tap_x, rail_y + tap_dy)


# ===========================================================================
# sheet furniture
# ===========================================================================
s.text(12, 16, "Workshop 4 mini-project - system interconnect", 5, bold=True)
s.text(12, 22.5,
       "ESP32-C3 SPI master / data-log collector  +  STM32F401 I2C sensor node "
       "acting as SPI log slave.   Sources: espc3/  and  stm/miniproject-4/", 2.2)
s.text(12, 27,
       "VISUALIZATION ONLY - block-level interconnect, protocol and DMA "
       "annotation. No decoupling, power tree, protection or footprints; not "
       "intended for PCB fabrication.", 2.2, color=(170, 40, 40, 1))

s.rect(10, 32, 292, 288, width=0.5, style="dash", color=ZONE)
s.text(13, 38.5, "STM32F401CCU6 NODE  -  I2C application + SPI log slave",
       2.8, bold=True, color=ZONE)

s.rect(300, 32, 584, 288, width=0.5, style="dash", color=ZONE)
s.text(303, 38.5, "ESP32-C3 NODE  -  SPI master, collector and log sinks",
       2.8, bold=True, color=ZONE)

# ===========================================================================
# ZONE A - STM32F401
# ===========================================================================
s.sym("U1", "MCU_ST_STM32F4:STM32F401CCUx", "STM32F401CCU6", 95, 165,
      props={"Board": "WeAct Black Pill", "Clock": "HSI 16 MHz, no PLL"})

# --- supply rails ---------------------------------------------------------
rail([("U1", "VBAT"), ("U1", 24), ("U1", 36), ("U1", 48)], 112, "+3V3", "#PWR001")
rail([("U1", 23), ("U1", 8)], 216, "GND", "#PWR002", tap_dx=-6.35, tap_dy=5.08)

# --- right side: ADC + SPI1 ----------------------------------------------
s.stub("U1", "PA0", 14, "LIGHT_A", "input")
s.stub("U1", "PA4", 14, "SPI_CS0_STM", "input")
s.stub("U1", "PA6", 14, "SPI_MISO", "output")
s.stub("U1", "PA7", 14, "SPI_MOSI", "input")
# PA5/SCK stubbed separately so the annotation lines up
s.stub("U1", "PA5", 14, "SPI_SCK", "input")

s.text(126, 119.5, "ADC1_IN0, 12-bit, 480-cycle", 1.6, color=MOD)
s.text(126, 122.5, "TIM2 TRGO 200 Hz -> DMA2 S0 Ch0", 1.6, color=MOD)
s.text(126, 148.5, "SPI1 slave, AF5, mode 0, hw NSS", 1.6, color=MOD)
s.text(126, 151.5, "TX: DMA2 S3 Ch3 circular, free-run", 1.6, color=MOD)
s.text(126, 154.5, "RX: DMA2 S2 Ch3 circular -> 64 B", 1.6, color=MOD)
s.text(126, 157.5, "debug sink (MOSI filler)", 1.6, color=MOD)

# --- left side: I2C1 + LED -----------------------------------------------
s.stub("U1", "PB6", 14, "I2C1_SCL")
s.stub("U1", "PB7", 14, "I2C1_SDA")
s.stub("U1", "PC13", 14, "STM_ACT_LED", "output")
s.text(66, 168, "I2C1 100 kHz - blocking HAL, CPU-driven", 1.6, color=MOD,
       justify="right bottom")
s.text(66, 145, "activity LED (active low)", 1.6, color=MOD,
       justify="right bottom")

# --- LDR divider ----------------------------------------------------------
s.sym("R1", "Sensor_Optical:LDR03", "LDR (GL5528)", 30, 66)
s.sym("R2", "Device:R", "10k", 30, 84)
a = s.pin("R1", "1")
s.wire(a, (a[0], a[1] - 5))
s.power("#PWR004", "+3V3", a[0], a[1] - 5)
b, c = s.pin("R1", "2"), s.pin("R2", "1")
midy = round((b[1] + c[1]) / 2, 2)
s.wire(b, (b[0], midy))
s.wire((b[0], midy), c)
s.wire((b[0], midy), (b[0] + 22, midy))
s.junction(b[0], midy)
s.glabel(b[0] + 22, midy, "LIGHT_A", 0, "output")
d = s.pin("R2", "2")
s.wire(d, (d[0], d[1] + 5))
s.power("#PWR005", "GND", d[0], d[1] + 5)
s.text(20, 56, "light sensor - resistive divider", 1.8, bold=True, color=MOD)
s.text(20, 100, "12-bit, 480-cycle sample.\n200 Hz TIM2-paced, averaged\n"
                "10 samples -> 20 records/s.", 1.6, color=MOD)

# --- PC13 LED -------------------------------------------------------------
s.sym("R3", "Device:R", "1k", 30, 232)
a = s.pin("R3", "1")
s.wire(a, (a[0], a[1] - 5))
s.power("#PWR006", "+3V3", a[0], a[1] - 5)
s.sym("D1", "Device:LED", "ACT", 46, 248, mirror="y")
b, c = s.pin("R3", "2"), s.pin("D1", "A")
s.wire(*route(b, c, "vh"))
d = s.pin("D1", "K")
s.wire(d, (d[0] + 10, d[1]))
s.glabel(d[0] + 10, d[1], "STM_ACT_LED", 0, "input")
s.text(20, 224, "PC13 activity LED", 1.8, bold=True, color=MOD)

# --- I2C1 pull-ups --------------------------------------------------------
s.sym("R4", "Device:R", "4k7", 140, 86)
s.sym("R5", "Device:R", "4k7", 152, 86)
for ref, pref in (("R4", "#PWR007"), ("R5", "#PWR008")):
    a = s.pin(ref, "1")
    s.wire(a, (a[0], a[1] - 5))
    s.power(pref, "+3V3", a[0], a[1] - 5)
b = s.pin("R4", "2")
s.wire(*route(b, (128, b[1] + 8), "vh"))
s.glabel(128, b[1] + 8, "I2C1_SCL", 180)
b = s.pin("R5", "2")
s.wire(*route(b, (128, b[1] + 16), "vh"))
s.glabel(128, b[1] + 16, "I2C1_SDA", 180)
s.text(132, 72, "I2C1 bus pull-ups", 1.8, bold=True, color=MOD)
s.text(128, 111, "(the Tiny RTC module carries", 1.5, color=MOD)
s.text(128, 114, "its own pull-ups too)", 1.5, color=MOD)

# --- Tiny RTC module ------------------------------------------------------
s.rect(168, 50, 288, 176, width=0.4, style="dash", color=MOD)
s.text(170, 47.5, "Tiny RTC I2C module  -  DS1307 + AT24C32 + CR2032 on one board",
       1.9, bold=True, color=MOD)

s.sym("U2", "Timer_RTC:DS1307Z+", "DS1307  (0x68)", 225, 92, field_at=(239, 100),
      props={"Note": "+56 B battery-backed NV-SRAM"})
s.sym("Y1", "Device:Crystal", "32.768 kHz", 194, 100)
s.sym("BT1", "Device:Battery_Cell", "CR2032", 204, 66)

# battery: '-' down to GND, '+' up and across to VBAT
a = s.pin("BT1", "-")
s.wire(a, (a[0], a[1] + 5))
s.power("#PWR009", "GND", a[0], a[1] + 5)
a, b = s.pin("BT1", "+"), s.pin("U2", "VBAT")
s.wire(*route(a, b, "vhv", mid=a[1] - 5))

# DS1307 VCC / GND
a, b = s.pin("U2", "VCC"), None
s.wire(a, (a[0], a[1] - 5), (a[0] - 8, a[1] - 5), (a[0] - 8, a[1] - 10))
s.power("#PWR010", "+3V3", a[0] - 8, a[1] - 10)
a = s.pin("U2", "GND")
s.wire(a, (a[0], a[1] + 5))
s.power("#PWR011", "GND", a[0], a[1] + 5)

# crystal
a, b = s.pin("U2", "X1"), s.pin("Y1", "2")
s.wire(a, (a[0] - 9, a[1]), (a[0] - 9, b[1]), b)
a, b = s.pin("U2", "X2"), s.pin("Y1", "1")
s.wire(a, (a[0] - 6, a[1]), (a[0] - 6, b[1] + 10), (b[0] - 4, b[1] + 10),
       (b[0] - 4, b[1]), b)

# DS1307 I2C + SQW
a = s.pin("U2", "SCL")
s.wire(a, (190, a[1]))
s.glabel(190, a[1], "I2C1_SCL", 180)
a = s.pin("U2", "SDA")
s.wire(a, (186, a[1]))
s.glabel(186, a[1], "I2C1_SDA", 180)
s.no_connect(*s.pin("U2", "SQW/OUT"))

s.sym("U3", "Memory_EEPROM:24LC32", "AT24C32  (0x50)", 225, 150, field_at=(240, 140),
      props={"Note": "4 KB - hourly light archive"})
a = s.pin("U3", "VCC")
s.wire(a, (a[0], a[1] - 5))
s.power("#PWR012", "+3V3", a[0], a[1] - 5)
a = s.pin("U3", "GND")
s.wire(a, (a[0], a[1] + 5))
s.power("#PWR013", "GND", a[0], a[1] + 5)
# A0..A2 strapped low -> 0x50
addr_x = s.pin("U3", "A0")[0] - 8
ys = []
for k in ("A0", "A1", "A2"):
    p = s.pin("U3", k)
    s.wire(p, (addr_x, p[1]))
    ys.append(p[1])
s.wire((addr_x, min(ys)), (addr_x, max(ys)))
for y in ys[1:-1]:
    s.junction(addr_x, y)
s.wire((addr_x, max(ys)), (addr_x, max(ys) + 8))
s.power("#PWR014", "GND", addr_x, max(ys) + 8)
a = s.pin("U3", "WP")
s.wire(a, (a[0] + 8, a[1]), (a[0] + 8, a[1] + 8))
s.power("#PWR015", "GND", a[0] + 8, a[1] + 8)
a = s.pin("U3", "SDA")
s.wire(a, (a[0] + 16, a[1]))
s.glabel(a[0] + 16, a[1], "I2C1_SDA", 0)
a = s.pin("U3", "SCL")
s.wire(a, (a[0] + 16, a[1]))
s.glabel(a[0] + 16, a[1], "I2C1_SCL", 0)
s.text(196, 170, "addresses on I2C1: 0x68 RTC, 0x50 EEPROM, 0x3C OLED",
       1.6, color=MOD)

# --- SSD1306 OLED on the STM ---------------------------------------------
s.sym("J1", "Connector_Generic:Conn_01x04", "SSD1306 OLED 128x64 (0x3C)",
      230, 224, props={"Note": "module header: GND / VCC / SCL / SDA"})
a = s.stub("J1", "Pin_1", 8)
s.wire(a, (a[0], a[1] + 10))
s.power("#PWR016", "GND", a[0], a[1] + 10)
a = s.stub("J1", "Pin_2", 14)
s.wire(a, (a[0], a[1] - 10))
s.power("#PWR017", "+3V3", a[0], a[1] - 10)
s.stub("J1", "Pin_3", 22, "I2C1_SCL")
s.stub("J1", "Pin_4", 22, "I2C1_SDA")
s.text(196, 244, "frame @ 1 Hz: clock, bus addresses,\n"
                 "light value, packet/byte rates.\n"
                 "SSD1306_Flush blocks ~92 ms.", 1.6, color=MOD)

# ===========================================================================
# ZONE B - ESP32-C3
# ===========================================================================
s.sym("U4", "RF_Module:ESP32-C3-DevKitM-1", "ESP32-C3-DevKitM-1", 370, 160,
      field_at=(330, 137), props={"Role": "SPI master / log collector"})

a = s.stub("U4", 13, 8)          # 5V
s.power("#PWR018", "+5V", a[0], a[1])
a = s.stub("U4", 1, 8)           # GND
s.power("#PWR019", "GND", a[0], a[1])
a = s.stub("U4", 2, 12)          # 3V3 out
s.wire(a, (a[0], a[1] - 8))
s.power("#PWR020", "+3V3", a[0], a[1] - 8)

# left side
s.stub("U4", 9, 16, "ESP_UART1_TX", "output")     # IO0
s.no_connect(*s.pin("U4", 10))                    # IO1
s.no_connect(*s.pin("U4", 4))                     # IO2
s.stub("U4", 5, 16, "SPI_CS3_SD", "output")       # IO3
s.stub("U4", 28, 16, "UART0_RX", "input")
s.stub("U4", 29, 16, "UART0_TX", "output")
s.stub("U4", 7, 16, "ESP_EN", "input")
s.text(352, 147, "UART1 TX @ 115200", 1.6, color=MOD, justify="right bottom")
s.text(352, 178, "UART0 console header", 1.6, color=MOD, justify="right bottom")

# right side - SPI2 / FSPI
s.sym("R6", "Device:R", "33R", 412, 134)
a = s.stub("U4", 20, 16)                          # IO4 = SCLK
b = s.pin("R6", "2")
s.wire(*route(a, b, "hv"))
c = s.pin("R6", "1")
s.wire(c, (c[0], c[1] - 6), (c[0] + 14, c[1] - 6))
s.glabel(c[0] + 14, c[1] - 6, "SPI_SCK", 0, "output")
s.text(424, 134, "series damping on SCK", 1.6, color=MOD)

s.stub("U4", 21, 16, "SPI_MISO", "input")         # IO5
s.stub("U4", 22, 16, "SPI_MOSI", "output")        # IO6
s.stub("U4", 23, 16, "SPI_CS0_STM", "output")     # IO7
s.stub("U4", 25, 16, "ESP_LED", "output")         # IO8
s.stub("U4", 26, 16, "I2C0_SCL", "output")        # IO9
s.stub("U4", 11, 16, "I2C0_SDA")                  # IO10
s.stub("U4", 18, 16, "USB_D_N")                   # IO18
s.stub("U4", 17, 16, "USB_D_P")                   # IO19

s.text(417, 145, "SPI2 (FSPI) master - SPI0/1 are flash-only,", 1.6, color=MOD)
s.text(417, 148, "DMA auto-channel, 512-byte blocks", 1.6, color=MOD)
s.text(417, 168, "I2C0 is brought up in firmware but no", 1.6, color=MOD)
s.text(417, 171, "display is fitted on this rig - IO9/IO10 spare", 1.6, color=MOD)
s.text(395, 190, "native USB Serial/JTAG (flash + console)", 1.6, color=MOD)

# --- IO8 activity LED -----------------------------------------------------
s.sym("R7", "Device:R", "1k", 450, 196)
a = s.pin("R7", "1")
s.wire(a, (a[0], a[1] - 5))
s.power("#PWR023", "+3V3", a[0], a[1] - 5)
s.sym("D2", "Device:LED", "SPI ACT", 464, 210, mirror="y")
b, c = s.pin("R7", "2"), s.pin("D2", "A")
s.wire(*route(b, c, "vh"))
d = s.pin("D2", "K")
s.wire(d, (d[0] + 10, d[1]))
s.glabel(d[0] + 10, d[1], "ESP_LED", 0, "input")
s.text(446, 182, "SPI activity LED - 15 ms pulse per block", 1.8, bold=True,
       color=MOD)
s.text(446, 216, "on the real DevKitM-1 this pin drives a", 1.5, color=MOD)
s.text(446, 219, "WS2812; a plain LED is shown here.", 1.5, color=MOD)

# --- UART1 log header -----------------------------------------------------
s.sym("J2", "Connector_Generic:Conn_01x02", "UART1 log sink header", 515, 62,
      mirror="y", field_at=(494, 52), props={"Note": "TX / GND, 115200 8N1"})
s.stub("J2", "Pin_1", 20, "ESP_UART1_TX", "input")
a = s.stub("J2", "Pin_2", 46)
s.wire(a, (a[0], a[1] + 12))
s.power("#PWR026", "GND", a[0], a[1] + 12)
s.text(452, 52, "log sink: serial", 1.8, bold=True, color=MOD)

# --- microSD --------------------------------------------------------------
s.sym("J3", "Connector:Micro_SD_Card", "microSD (FAT32, datalog.txt)",
      505, 250, mirror="y", field_at=(440, 240),
      props={"Note": "SDSPI + FATFS, synchronous"})
s.stub("J3", "DAT3/CD", 18, "SPI_CS3_SD", "input")
s.stub("J3", "CMD", 18, "SPI_MOSI", "input")
s.stub("J3", "CLK", 18, "SPI_SCK", "input")
s.stub("J3", "DAT0", 18, "SPI_MISO", "output")
s.no_connect(*s.pin("J3", "DAT1"))
s.no_connect(*s.pin("J3", "DAT2"))
a = s.stub("J3", "VDD", 40)
s.wire(a, (a[0], a[1] - 14))
s.power("#PWR027", "+3V3", a[0], a[1] - 14)
a = s.stub("J3", "VSS", 46)
s.wire(a, (a[0], a[1] + 14))
s.power("#PWR028", "GND", a[0], a[1] + 14)
a = s.stub("J3", "SHIELD", 8)
s.wire(a, (a[0], a[1] + 8))
s.power("#PWR029", "GND", a[0], a[1] + 8)
s.text(450, 232, "log sink: microSD - shares SPI2, own CS (IO3)",
       1.8, bold=True, color=MOD)
s.text(424, 274, "appends text lines + fflush;", 1.5, color=MOD)
s.text(424, 277, "no DMA. DAT1/DAT2 unused.", 1.5, color=MOD)

# ===========================================================================
# annotation panel
# ===========================================================================
PANEL_Y, PANEL_H, TS = 294, 86, 1.45

s.textbox(10, PANEL_Y, 140, PANEL_H, """SPI LOG STREAM - MASTER / SLAVE CONTRACT
source of truth: espc3/src/LogProtocol.hpp + LogPacket.hpp
(the STM slave mirrors these constants in Src/log_emission.c)

Topology   Star. The ESP32-C3 is the only master; every other node
           is a slave on the shared SCK/MOSI/MISO with its own CS.
           Slaves never talk to each other.
Mode       SPI mode 0 (CPOL=0, CPHA=0), MSB first, 8-bit, 2 MHz.
           The ceiling is the F401 slave, not the master: it runs on
           HSI with no PLL, so PCLK2 = 16 MHz and SCK wants to stay
           well under PCLK/4 = 4 MHz.
Direction  One-way, best effort. The master only clocks 0xFF dummy
           bytes on MOSI; the slave continuously presents self-framed
           packets on MISO. No ack, no time sync, no command set.
Packet     [18-byte header][payload 0..255 B][CRC-16/CCITT-FALSE]
           magic 0xA5 | status | seq | objId[4] | valueType |
           payloadLen ... little-endian; 20 B min, 275 B max.
Framing    The parser resyncs on the next 0xA5 whose CRC checks out,
           so torn frames at the ring seam and 0xFF idle lines are
           simply rejected rather than mis-parsed.
Status     0x00 ENTRY, 0x01 NOENTRY (heartbeat);
           flags 0x80 OVERFLOW (slave dropped), 0x40 NEARFULL.
Records    TIME (VT_DATETIME, 1 Hz from the DS1307) and
           LGHT (ADC average, 20 Hz).
Rates      The slave emits ~470 B/s; the master drains ~125 KB/s in
           512-byte blocks, so a faster clock re-reads the same ring
           more often - it buys latency margin, not throughput.""",
           TS, color=INK)

s.textbox(154, PANEL_Y, 140, PANEL_H, """STM32F401CCU6 - PERIPHERAL AND DMA MAP
stm/miniproject-4/  (STM32CubeIDE, HAL; comments in Ukrainian)

Clock       HSI 16 MHz, no PLL. PCLK2 = 16 MHz.
PA0         ADC1_IN0, 12-bit, 480-cycle sample time.
TIM2        PSC 1599 / ARR 49 -> TRGO on update = 200 Hz.
            Hardware-triggers every ADC conversion.
DMA2 S0 Ch0 ADC1 -> 20-sample circular ring, periph->mem,
            half-word, half + full transfer IRQ. No CPU per sample;
            each half-buffer is averaged into one LGHT record, so
            20 records/s. Flags (not counters) mark the halves - a
            long blocking frame can silently drop one sample.
SPI1 slave  PA4 NSS (SPI_NSS_HARD_INPUT), PA5 SCK, PA6 MISO,
            PA7 MOSI, AF5, mode 0, MSB first, CRC off.
            Added by hand - not owned by miniproject-4.ioc.
DMA2 S3 Ch3 mem->periph, CIRCULAR, free-running: the TX ring is
            wired straight into SPI1->DR and never stops. The master
            reads whatever currently sits under the write pointer;
            the XferCplt IRQ only counts laps for the byte-rate stat.
DMA2 S2 Ch3 periph->mem, CIRCULAR: SPI1->DR into a 64-byte sink that
            captures the master's MOSI filler. Debug drain only.
I2C1        PB6 SCL / PB7 SDA, 100 kHz, blocking HAL, CPU-driven.
            DS1307 0x68, AT24C32 0x50, SSD1306 0x3C.
            i2c_bus.c can bit-bang 9 clocks + a manual STOP to free a
            slave stuck holding SDA, then DeInit / HAL_I2C_Init.
PC13        Activity LED, active low, pulsed on SPI traffic.
EEPROM      One light byte per hour into the AT24C32; the next-write
            pointer is stored in the EEPROM itself.""",
           TS, color=INK)

s.textbox(298, PANEL_Y, 140, PANEL_H, """ESP32-C3-DevKitM-1 - PERIPHERAL AND DMA MAP
espc3/  (PlatformIO + ESP-IDF, C++, FreeRTOS)

SPI2 / FSPI SCLK IO4, MISO IO5, MOSI IO6. SPI0/1 are reserved for
            the flash. DMA on an auto-assigned channel, 512-byte
            blocks. The sweep is pipelined: device i's block clocks
            on the wire while device i-1's block is parsed, so at
            most two devices are attached and only one transfer is
            ever on the bus.
CS scan     IO7 = the STM32 log slave. drainPresent() runs flat-out
            (its SPI transfers block and thereby yield the CPU);
            probeAbsent() runs once every 1000 ms purely to notice
            hot-plug. Spare CS pins are reserved for a second slave
            (an ESP32-S3 laser node) - not wired on this rig.
IO3         microSD CS. Same shared bus, dedicated CS, SDSPI +
            FATFS, synchronous - no DMA on this path.
I2C0        SDA IO10 / SCL IO9, 400 kHz. Brought up in firmware
            but no display is fitted on this rig - the pins are
            spare and no pull-ups are needed.
UART1       TX on IO0 @ 115200 -> serial log sink. Kept off UART0 so
            it does not collide with the console.
UART0       IO20 RX / IO21 TX - console header.
USB         IO18 D- / IO19 D+ - native USB Serial/JTAG, used for
            flashing and the console. It re-enumerates on every
            reset, hence the 2 s delay at the top of app_main.
IO8         Activity LED, active low, 15 ms pulse per block.
Sinks       Every record fans out to all LogsTarget sinks. In use
            here: UART1 and the microSD card. The OLED sink is still
            compiled in but no display is attached.
            LogRecord::data points into a shared buffer valid only
            during write() - a sink that keeps the bytes must copy
            them first.
Pin limits  IO11-17 = SPI flash, IO18/19 = USB, IO20/21 = UART0,
            IO2/8/9 = strapping. Validate before moving pins.""",
           TS, color=INK)

s.textbox(442, PANEL_Y, 142, PANEL_H, """INTER-BOARD LINK  -  common ground required

net            ESP32-C3     STM32F401     microSD
SPI_SCK        IO4  out     PA5  SCK      CLK
SPI_MOSI       IO6  out     PA7  MOSI     CMD  (DI)
SPI_MISO       IO5  in      PA6  MISO     DAT0 (DO)
SPI_CS0_STM    IO7  out     PA4  NSS       -
SPI_CS3_SD     IO3  out      -            DAT3/CS

KNOWN ISSUES AND DESIGN NOTES  (from README.md)

- A failed wire once produced wrong data over SPI. Keep the
  harness short and check continuity before blaming firmware.
- R6 (33R) in series with SCK damps ringing; with it, 2 MHz has
  margin. Step back to 1 MHz or 250-500 kHz if drops reappear -
  the symptom is CRC-rejected frames, which look like a silent
  fall in record rate rather than an error.
- The microSD mount failed intermittently whenever the STM32 was
  powered. SPI1 uses hardware NSS, and until the collector's
  first attach() that CS pin was left floating, so noise on it
  could make the STM drive MISO during an SD transaction and
  corrupt it. Fixed in firmware by driving every CS high right
  after bus init; a hardware pull-up on each CS/NSS line is the
  fuller fix, since it also covers the pre-firmware boot window.
- I2C1 needs external pull-ups (R4/R5); the Tiny RTC module
  already carries its own. I2C0 has no device fitted.
- The two boards are separate builds and separate power domains;
  tie the grounds together or the link will not work.

SCOPE: visualization only. No decoupling, power tree, ESD or
level shifting, and no footprints are shown. Do not fabricate
from this sheet.""",
           TS, color=INK)

s.save(OUT)
print("wrote", OUT, os.path.getsize(OUT), "bytes")
