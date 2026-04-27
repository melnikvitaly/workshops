# Workshop 2-1 — Embedded C++ on ESP32

Board: **ESP32-C3 DevKitM-1**

1. Non-blocking blink with `enum class LEDState`, `millis()` — [Led.h](src/hardware/Led.h), [LedController.h](src/LedController.h)
2. `constexpr` pin/timing, no magic numbers — [main.cpp](src/main.cpp), [Config.h](src/Config.h)
3. Config class with `static constexpr` — [Config.h](src/Config.h)
4. Superloop timing per 100000 iterations — [LoopTracker.h](src/LoopTracker.h)
5. Button with ISR, `volatile bool`, mode cycling BLINKING→ON→OFF — [Button.h](src/hardware/Button.h), [main.cpp](src/main.cpp)
