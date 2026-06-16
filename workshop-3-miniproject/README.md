# Mini Project 3

## Task
LED laser on gimbal (2 axises) that are controlled by potentiometer and encoder
## Parts
2 servo SG90 for [gimbal](https://www.printables.com/model/1042622-cheap-fpv-gimbal-pantilt)
1 [LED laser](https://uamper.com/%D0%9B%D0%B0%D0%B7%D0%B5%D1%80-5%D0%BC%D0%92%D1%82-650%D0%BD%D0%BC-%D1%82%D0%BE%D1%87%D0%BA%D0%B0)
    - connected via relay
1 - potentiometer
1 - encoder 
power for servoce is from external source

ESP-S3 DevKit

  
### Known issues/todo LATER
- switching of relay during rest/boot/flash
  - control power of relay separately
  - understand exactly behavior of the PINs
- preserve angles across reboots
  - save to NVS (Non-Volatile Storage) 
- Review/Rewrite Encoder ~~debounce on encoder/ PCNT~~ - src\drivers\EncoderPcnt.hpp
- more granular movement within viewport/Encoder precision toggle
- Pot/Encoder calibration - understand that movement is linearly changed with rotation
- deadzone for Encoder - when counter is large and we try rotate back then nothing changed
- ~~make center of view port as (0,0) for inputs~~ (done: ViewPort is centre+size; inputs work in centred coords and ViewPort.translate() places them)
- Commands for flash led is delayed as in the same queue as movement
- ~~Encoder: stop increment out of the min/max/viewport~~
- ~~"Command currentTarget()" does not look correct.~~ - replaced by update() calls that prepare commands
- use integers instead of float for coordinates   (will not do)
- attach real joystick (later)
- ~~Try automate movement~~ AutoInput only for now
- ~~apply program filters~~ - sma/ema were tried


### ESP-IDF Components in PlatformIO: Quick Overview





* **`idf_component.yml` (Manifest):** Create this in your `src/` directory to list the ESP-IDF components your project needs. **(Commit to Git)**
* **`dependencies.lock` (Lockfile):** Auto-generated during the build process to lock in exact component versions for reproducibility. **(Commit to Git)**
* **`managed_components/` (Downloads):** Auto-generated folder where PlatformIO downloads the component source code. **(Do NOT commit / add to `.gitignore`)**

**Why use this instead of `lib_deps`?**
While `lib_deps` (in `platformio.ini`) is great for Arduino, `idf_component.yml` is required for native ESP-IDF projects to properly connect to the official Espressif Registry and handle complex CMake build scripts.


** Make vs. CMake in PlatformIO**

* **Make is deprecated** in modern ESP-IDF.
* **CMake is the standard.** PlatformIO acts as a wrapper, delegating the actual build process to ESP-IDF's native CMake system.
* CMake triggers the Component Manager, which reads your `idf_component.yml`, downloads dependencies to `managed_components/`, and compiles them alongside your code.

**Finding & Adding Components**

* **Find:** Browse the official **[Espressif Component Registry](https://components.espressif.com/)**. Do not use PlatformIO's built-in library search (`lib_deps`).
* **Add:** Copy the provided YAML snippet from the registry page directly into your project's `src/idf_component.yml`.
```yaml
dependencies:
  espressif/led_strip: "^3.0.0"

```
* **CLI Alternative:** Run `idf.py add-dependency "espressif/led_strip^3.0.0"` in your ESP-IDF terminal to automatically update the YAML file.
