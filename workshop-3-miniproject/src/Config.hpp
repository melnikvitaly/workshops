#pragma once
#include <cstdint>
#include <driver/ledc.h>
#include <hal/adc_types.h>
#include <utils/ViewPort.hpp>

// All input sources are built; the on-board BOOT button cycles between them at
// runtime. INPUT_SOURCE just picks which one is active at boot. These values
// double as indices into the `inputs[]` array in main.cpp, so keep them in sync.
#define INPUT_MANUAL 0   // ManualInput   - pot (pan) + encoder (tilt)
#define INPUT_AUTO 1     // AutoInput     - hands-off circle + diagonal cross
#define INPUT_JOYSTICK 2 // JoystickInput - 2-axis analog joystick

#define INITIAL_INPUT_SOURCE INPUT_MANUAL

// Application-level tuning. Hardware wiring lives in Pinout.hpp; the reusable
// classes keep their own internal defaults, and main.cpp passes these values
// in explicitly so all the "knobs" are in one place.
namespace config
{
    // --- Servo / PWM ---------------------------------------------------------
    constexpr ledc_timer_bit_t SERVO_PWM_RES = LEDC_TIMER_14_BIT; // fine steps at 50 Hz
    constexpr uint32_t SERVO_FREQ_HZ = 50;                        // SG90 expects 50 Hz
    constexpr uint16_t SERVO_MIN_US = 500;                        // pulse at one end stop
    constexpr uint16_t SERVO_MAX_US = 2500;                       // pulse at other end stop
    constexpr float SERVO_MIN_ANGLE = 0.0f;
    constexpr float SERVO_MAX_ANGLE = 180.0f;

    // LEDC resource assignment for the two servo channels.
    constexpr ledc_channel_t PAN_PWM_CHANNEL = LEDC_CHANNEL_0;
    constexpr ledc_timer_t PAN_PWM_TIMER = LEDC_TIMER_0;
    constexpr ledc_channel_t TILT_PWM_CHANNEL = LEDC_CHANNEL_1;
    constexpr ledc_timer_t TILT_PWM_TIMER = LEDC_TIMER_1;

    // --- Buzzer / PWM --------------------------------------------------------
    // The buzzer owns its OWN timer (TIMER_2) because it retunes frequency per
    // tone; the servos keep TIMER_0/1 at a steady 50 Hz, so the buzzer can never
    // disturb them. Channel 2 is otherwise unused.
    constexpr ledc_channel_t BUZZER_PWM_CHANNEL = LEDC_CHANNEL_2;
    constexpr ledc_timer_t BUZZER_PWM_TIMER = LEDC_TIMER_2;
    constexpr ledc_timer_bit_t BUZZER_PWM_RES = LEDC_TIMER_10_BIT;

    // --- Potentiometer / ADC -------------------------------------------------
    constexpr adc_atten_t POT_ADC_ATTEN = ADC_ATTEN_DB_12; // ~0..3.3 V range
    constexpr int POT_FILTER_WINDOW = 16;                  // moving-average samples
    constexpr float POT_FILTER_ALPHA = 0.8f;               // EMA smoothing factor [0..1]

    // --- Gimbal travel limits (degrees) -------------------------------------
    // -1 maps to MIN, +1 to MAX, 0 to their midpoint.
    constexpr float GIMBAL_PAN_MIN = 15.0f;
    constexpr float GIMBAL_PAN_MAX = 100.0f;
    constexpr float GIMBAL_TILT_MIN = 46.0f;
    constexpr float GIMBAL_TILT_MAX = 125.0f;

    // --- Encoder -------------------------------------------------------------
    constexpr int ENC_STEPS_PER_DETENT = 4;
    constexpr float ENC_SPAN_DETENTS = 20.0f;  // detents centre -> full deflection
    constexpr float ENC_FILTER_ALPHA = 0.4f;  // EMA on tilt so the servo glides
                                               // between detents instead of jumping

    // --- Input source --------------------------------------------------------
    // AutoInput timing (hands-off demo: circle, then a diagonal corner cross).
    constexpr uint32_t AUTO_CIRCLE_MS = 6000; // time for one full circle
    constexpr uint32_t AUTO_CROSS_MS = 4000;  // time for the full diagonal cross

    // --- Encoder push button -------------------------------------------------
    constexpr uint32_t BTN_DEBOUNCE_MS = 25;
    constexpr uint32_t BTN_LONG_PRESS_MS = 800; // hold this long -> laser constant on

    // --- Laser flash ---------------------------------------------------------
    constexpr uint32_t LASER_FLASH_MS = 1000; // README: click flashes 1 s
    constexpr bool RELAY_ACTIVE_HIGH = false; // this module energizes on LOW

    // --- Buzzer tones (laser feedback only) ---------------------------------
    constexpr uint32_t BEEP_FIRE_HZ = 2200;
    constexpr uint32_t BEEP_FIRE_MS = LASER_FLASH_MS; // tone lasts the whole flash
    constexpr uint32_t BEEP_ON_HZ = 2600;
    constexpr uint32_t BEEP_OFF_HZ = 1400;
    constexpr uint32_t BEEP_TOGGLE_MS = 90;

    // --- Loop timing ---------------------------------------------------------
    // The main loop runs fast (LOOP_PERIOD_MS) so the encoder is polled often
    // enough not to miss steps. Reading the pot, recomputing (x,y), commanding
    // the servos and logging do NOT need to happen that often: the servos only
    // accept a fresh pulse every 20 ms (50 Hz) and logging faster would flood
    // the console. So that work is rate-limited to UPDATE_PERIOD_MS via a
    // timestamp check, leaving the fast encoder polling untouched.
    constexpr uint32_t LOOP_PERIOD_MS = 2;    // fast loop (encoder polling)
    constexpr uint32_t UPDATE_PERIOD_MS = 20; // gimbal/log update (~50 Hz)

    // --- Logging -------------------------------------------------------------
    constexpr float LOG_EPSILON = 0.01f; // min (x,y) change worth a log line

    // --- Status LED colours --------------------------------------------------
    struct Rgb
    {
        uint8_t r, g, b;
    };
    constexpr Rgb LED_IDLE = {0, 16, 0};  // green: idle / tracking
    constexpr Rgb LED_LASER = {64, 0, 0}; // red:   laser firing

    // Gimbal working ViewPort, expressed directly in servo angles (degrees):
    // pan [min,max] then tilt [min,max]. This is the sub-window the inputs reach
    // within the full gimbal travel limits above.
    constexpr static ViewPort InitialViewPort = ViewPort::fromBounds(
        56.0f, 79.0f,    // pan  (degrees)
        20, 115);  // tilt (degrees)
}
