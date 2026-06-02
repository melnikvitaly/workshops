# Workshop 3-4: Playing Sound on a Buzzer (PWM)

## Task

Non-blocking audio player for a buzzer (PWM/LEDC) with a multi-note synthesizer.
Uses a fixed 50 ms tick — no FreeRTOS tasks or delays for timing.

## Solution

### Architecture

| File | Role |
|---|---|
| [src/Notes.hpp](src/Notes.hpp) | Note frequency constants (`Notes::C4`, `Notes::G4`, …) |
| [src/Melody.hpp](src/Melody.hpp) | `Note` struct, melody arrays, `MelodyDef` table |
| [src/MelodyPlayer.hpp](src/MelodyPlayer.hpp) | `MelodyPlayer` — tick-driven state machine |
| [src/hardware/Buzzer.hpp](src/hardware/Buzzer.hpp) | Buzzer wrapper: `playNote(freq)` / `silence()` |
| [src/hardware/PWM.hpp](src/hardware/PWM.hpp) | LEDC driver with runtime `setFreq()` |
| [src/main.cpp](src/main.cpp) | Wiring: `esp_timer` periodic callback → `player.tick()` |

### How it works

`esp_timer` fires every **50 ms**. Each call advances `MelodyPlayer::tick()`:
- Loads the next note when the current note's tick count expires.
- Calls `Buzzer::playNote(freq)` to change PWM frequency and set 50% duty.
- Silences the buzzer on the **last tick** of each note for articulation (prevents repeated same notes from blending).
- Cycles through melodies automatically; logs the name on each transition.

### Timing

```
8 ticks × 50 ms = 400 ms = quarter note (~150 BPM)
```

| Duration | Ticks |
|---|---|
| Eighth note | 4 |
| Quarter note | 8 |
| Dotted quarter | 12 |
| Half note | 16 |
| Whole note | 32 |

### Melodies

Five melodies cycle automatically: Twinkle Twinkle Little Star, Baby Shark, Jingle Bells, We Will Rock You, Old MacDonald.

### Hardware

- Buzzer on **GPIO 8** (`BUZZER_PIN` in [src/main.cpp](src/main.cpp), line 10)
- Board: ESP32-S3-DevKitC-1
