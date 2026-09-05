

## Coding Standards
- Use ESP-IDF error checking macros (`ESP_ERROR_CHECK`).
- Follow standard FreeRTOS naming and task creation guidelines.
- Avoid dynamic memory allocation in high-frequency interrupt service routines (ISRs).
- No "magic" values in the code
- All functions are on the same level of abstraction
- SOLID-rules are applied to classes/functions
- variable names are meaningful
- No verbose or obvious comments in the codebase


## Do / Don't Rules
- **DO** verify pin definitions against the target board's schematic before writing GPIO code.
- **DO** run `pio run` to check for compilation errors after changing dependencies in `platformio.ini`.
- **DON'T** hardcode Wi-Fi credentials or API keys; use `include/secrets.h` (and keep it git-ignored).
- **DON'T** block the main execution loop (`loop()`) with long `delay()` calls; use non-blocking timers (e.g., `millis()`).