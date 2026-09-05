

## Do / Don't Rules
- **DO** wrap ESP-IDF calls in error-checking macros (`ESP_ERROR_CHECK`).
- **DO** follow standard FreeRTOS naming and task-creation guidelines.
- **DON'T** allocate dynamic memory in high-frequency interrupt service routines (ISRs).
- **DON'T** use "magic" values in the code.
- **DO** keep every function at a single level of abstraction.
- **DO** apply SOLID rules to classes/functions.
- **DO** use meaningful variable names.
- **DON'T** write verbose or obvious comments in the codebase.
- **DO** verify pin definitions against the target board's schematic before writing GPIO code.
- **DO** run `pio run` to check for compilation errors after changing dependencies in `platformio.ini`.
- **DON'T** hardcode Wi-Fi credentials or API keys; use `include/secrets.h` (and keep it git-ignored).
- **DON'T** block the main execution loop (`loop()`) with long `delay()` calls; use non-blocking timers (e.g., `millis()`).
