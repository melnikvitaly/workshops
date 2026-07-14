# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository shape

This is a workshop mini-project made of **two independent firmware projects** that are meant to talk to each other over SPI but are built and flashed separately. Each has its own `CLAUDE.md` with build steps and architecture — read the one for the project you're editing:

- **`espc3/`** — ESP32-C3 firmware, the **SPI master** / data-log collector. PlatformIO + ESP-IDF, C++. The active development target. See [`espc3/CLAUDE.md`](espc3/CLAUDE.md).
- **`stm/miniproject-4/`** — STM32F401 firmware, an STM32CubeIDE (Eclipse) project: an I2C application (DS1307 RTC, SSD1306 OLED, external EEPROM light logging via ADC) that **also acts as the SPI log-slave** for the ESP master. See [`stm/miniproject-4/CLAUDE.md`](stm/miniproject-4/CLAUDE.md).

The two ends communicate over a one-way, best-effort SPI log stream. The wire format and the master/slave contract are defined once in `espc3/src/LogProtocol.hpp` (the source of truth); the STM slave mirrors those constants in `Src/log_emission.c` — keep the two in sync.

`openspec/`, `.claude/`, and `.github/` hold OpenSpec tooling/skills, not firmware.
