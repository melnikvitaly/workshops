## Structure

The final project is **fully autonomous**. Sources are **copied** in, never
referenced across directories; nothing in the build reaches outside this folder.

```text
workshop-7-final_project/
  README.md              overview/plan of the project
  TASKS.md               list of tasks to implement
  REVIEW.md              temporal review of this project by AI
  firmware/
    aim/                 gimbal controller — ESP32-S3 (ESP-IDF / PlatformIO)
      src/
        drivers/         Uart, Pwm, Sdcard, Ssd1306, LaserDriver
        parts/           Gimbal, Laser, StatusLed
        tasks/           ctrl, safety, link_uart, logger, ui (+ link_net, radio: Phase 1)
        transport/       ITransport + Uart (Mqtt / EspNow: Phase 1)
        utils/           Pid, RingQueue, Crc8
        Config.hpp  ConfigStore.hpp  StateMachine.hpp  Pinout.hpp
    pilot/               wireless remote — ESP32-C3
    vault/               storage controller — STM32 (Phase 1)
  eye/                   vision host — PC (Python)
    camera/              detection, overlay, controls, serial link
    tools/               fuzz_link.py, log plotting, soak analysis
  hardware/
    aim-board/           ESP32-S3 controller — KiCad, DRC, plots, BOM, DESIGN-REVIEW.md
    pilot-board/         ESP32-C3 remote — Phase 1
  mqtt/config/           mosquitto.conf, credentials (not committed) — Phase 1
  docs/
    architecture.md      documentation of the project
    coding.md            coding conventions
    …                    interfaces, protocol, bringup
  VERIFICATION/          how project will be evaluated — requirements sheet + coverage
```

**Isolation rules:**
- `REVIEW.md` MUST NOT be referenced from other files.
- `VERIFICATION/` MUST NOT be referenced by other parts of project.

## Build & Development Commands
- **Build project:** `pio run`
- **Build specific environment:** `pio run -e <env_name>`
- **Upload firmware:** `pio run -t upload`
- **Upload to specific port:** `pio run -t upload --upload-port /dev/cu.usbserial-XXXX`
- **Monitor serial output:** `pio device monitor`
- **Clean build files:** `pio run -t clean`

## Done

- docs updated
- вщсі.coding.md tolarated
