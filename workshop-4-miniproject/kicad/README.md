# kicad/ — system interconnect schematic

KiCad drawings of the whole mini-project rig: the **ESP32-C3 SPI master / log
collector** (`espc3/`) and the **STM32F401 I2C sensor node that also acts as the
SPI log slave** (`stm/miniproject-4/`), drawn together with the protocol, bus and
DMA behaviour annotated.

Two sheets, two levels of zoom — the **detail sheet** has every pin and symbol,
the **block sheet** has only nodes and buses and is the one the top-level
`README.md` embeds.

| file | what it is |
| --- | --- |
| `miniproject-4.kicad_sch` | detail sheet: symbols, pins, nets (KiCad 10, A2) |
| `miniproject-4.kicad_pro` | project file so it opens as a project |
| `miniproject-4.pdf` / `.svg` | exports, for reading without KiCad |
| `miniproject-4-blocks.kicad_sch` | block sheet: high-level topology (A3) |
| `miniproject-4-blocks.kicad_pro` | project file for the block sheet |
| `miniproject-4-blocks.pdf` / `.svg` | exports; the `.svg` is embedded in the top-level `README.md` |
| `generator/` | the Python that produced both sheets (see below) |

## Scope — visualization only

**This is not a PCB-ready design and must not be fabricated from.** It is a
readable picture of how the two boards and their peripherals hang together. It
deliberately omits decoupling, the power tree, protection/ESD, level shifting,
and all footprints. ERC reports ~40 violations, all of the expected kinds:
unused STM32 GPIO (`pin_not_connected`), and nets with no driving source because
there is no regulator on the sheet (`pin_not_driven`, `power_pin_not_driven`).
There are no connection or conflict errors — the netlist matches the firmware.

## What is on the sheet

Two dashed zones, plus a four-panel annotation block along the bottom:

- **STM32F401CCU6 node** — the MCU, the LDR divider into `PA0`, the Tiny RTC
  module (DS1307 `0x68` + AT24C32 `0x50` + CR2032 + 32.768 kHz crystal), the
  SSD1306 `0x3C` on I2C1, the I2C1 pull-ups and the PC13 activity LED.
- **ESP32-C3 node** — the DevKitM-1, the SCK series damping resistor, the IO8
  activity LED, and the two log sinks actually in use: a UART1 header and the
  microSD card on the shared SPI2 bus with its own CS. I2C0 (IO9/IO10) is
  brought up in firmware but no display is fitted on this rig, so the OLED sink
  and its bus pull-ups are not drawn.
- **Annotation panels** — the SPI log stream master/slave contract (mode, packet
  layout, CRC, framing, rates), the STM32 peripheral/DMA map (TIM2 → ADC1 →
  DMA2 S0, the free-running circular DMA2 S3 TX and DMA2 S2 RX debug drain), the
  ESP32-C3 peripheral map (SPI2 DMA blocks, CS scan, sinks, pin constraints), and
  the inter-board net table plus the known issues from the top-level `README.md`.

Cross-block connections are drawn as **global labels** rather than long wires, so
the buses stay readable; same-named labels are the same net. The netlist is
correct — `SPI_SCK` / `SPI_MOSI` / `SPI_MISO` / `SPI_CS0_STM` / `SPI_CS3_SD`,
both I2C buses and both supplies all resolve as intended.

Pin assignments were taken from the firmware, not from the prose docs:
`espc3/src/main.cpp` for the ESP side (note it disagrees with the pin list in
`espc3/CLAUDE.md` — `main.cpp` wins), and `Src/main.c`,
`Src/stm32f4xx_hal_msp.c`, `Src/log_emission.c`, `Inc/config.h` for the STM side.

## Regenerating

The sheet is generated, so it can be rebuilt when pins change:

```sh
cd kicad/generator
python build.py ../miniproject-4.kicad_sch
```

`ksym.py` reads pin geometry straight out of the installed KiCad symbol
libraries, `schlib.py` is a small `.kicad_sch` writer that computes wire
endpoints from that geometry, and `build.py` is the layout itself. Editing the
schematic in Eeschema by hand is fine too — just don't re-run the generator
afterwards, it overwrites the file.
