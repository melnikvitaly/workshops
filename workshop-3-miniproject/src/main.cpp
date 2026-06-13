#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "Pinout.hpp"
#include "Config.hpp"
#include "PWM.hpp"
#include "ADC.hpp"
#include "Relay.hpp"
#include "StatusLed.hpp"
#include "Encoder.hpp"
#include "Button.h"
#include "Buzzer.hpp"
#include "Servo.hpp"
#include "Potentiometer.hpp"
#include "GimbalController.hpp"
#include "ViewPort.hpp"
#include "Input.hpp"
#include "ManualInput.hpp"
#include "AutoInput.hpp"
#include "JoystickInput.hpp"
#include "GimbalLogger.hpp"
#include "Laser.hpp"
#include "Beeper.hpp"

static const char *TAG = "GIMBAL";

// --- hardware ---------------------------------------------------------------
static PWM       panPwm (pinout::SERVO_PAN,  config::PAN_PWM_CHANNEL,  config::PAN_PWM_TIMER,
                         config::SERVO_FREQ_HZ, config::SERVO_PWM_RES);
static PWM       tiltPwm(pinout::SERVO_TILT, config::TILT_PWM_CHANNEL, config::TILT_PWM_TIMER,
                         config::SERVO_FREQ_HZ, config::SERVO_PWM_RES);
static Servo     panServo (panPwm);
static Servo     tiltServo(tiltPwm);
static ADC       potAdc(pinout::POT_ADC_UNIT, pinout::POT_ADC_CHAN, config::POT_ADC_ATTEN);
static Encoder   encoder(pinout::ENC_A, pinout::ENC_B);
static Button    encButton(pinout::ENC_SW, LOW, config::BTN_DEBOUNCE_MS, config::BTN_LONG_PRESS_MS);
static Button    bootButton(pinout::BOOT_BTN, LOW, config::BTN_DEBOUNCE_MS, config::BTN_LONG_PRESS_MS);
static Relay     laserRelay(pinout::RELAY, config::RELAY_ACTIVE_HIGH);
static StatusLed status(pinout::STATUS_LED);
static PWM       buzzerPwm(pinout::BUZZER, config::BUZZER_PWM_CHANNEL, config::BUZZER_PWM_TIMER,
                          config::BEEP_FIRE_HZ, config::BUZZER_PWM_RES);
static Buzzer    buzzer(buzzerPwm);

// --- logic ------------------------------------------------------------------
static Potentiometer   pot(potAdc);
static GimbalController gimbal(panServo, tiltServo,
                              config::GIMBAL_PAN_MIN,  config::GIMBAL_PAN_MAX,
                              config::GIMBAL_TILT_MIN, config::GIMBAL_TILT_MAX);
static const ViewPort   viewport{ config::VIEW_X_MIN, config::VIEW_X_MAX,
                                  config::VIEW_Y_MIN, config::VIEW_Y_MAX };

// All input sources are built; the BOOT button cycles which one is active at
// runtime. TODO: joystick needs a second ADC axis; it reuses `pot` for both
// axes until the Y-axis channel is wired up in Pinout.hpp.
static ManualInput      manualInput(pot, encoder, viewport, config::ENC_SPAN_DETENTS);
static AutoInput        autoInput(viewport, config::AUTO_CIRCLE_MS, config::AUTO_CROSS_MS);
static JoystickInput    joystickInput(pot, pot, viewport);

// Index order matches the INPUT_* constants in Config.hpp, so INPUT_SOURCE
// selects the source that is active at boot.
static Input* const     inputs[]    = { &manualInput, &autoInput, &joystickInput };
static const char*      inputNames[] = { "manual", "auto", "joystick" };
static constexpr int    INPUT_COUNT = sizeof(inputs) / sizeof(inputs[0]);
static int              activeInput = INPUT_SOURCE;

static Laser            laser(laserRelay, config::LASER_FLASH_MS);
static Beeper           beeper(buzzer);
static GimbalLogger     telemetry(TAG, config::LOG_EPSILON);

static uint32_t nowMs() { return pdTICKS_TO_MS(xTaskGetTickCount()); }

static void setStatus(const config::Rgb &c) { status.rgb(c.r, c.g, c.b); }

extern "C" void app_main()
{
    laser.init();      // FIRST: drive the relay to its OFF level ASAP to
                       // minimise the boot-time click (full fix is a pull-up)
    gimbal.init();     // inits both servos (and their PWM channels)
    for (Input* in : inputs)
        in->init();    // each source inits its own hardware (ADC init is idempotent)
    encButton.init();  // inits the encoder push button GPIO
    bootButton.init(); // inits the on-board BOOT button GPIO
    beeper.init();     // inits the buzzer PWM channel (its own LEDC timer)
    status.init();
    setStatus(config::LED_IDLE);

    // BOOT button click -> cycle to the next input source.
    bootButton.onRelease([] {
        activeInput = (activeInput + 1) % INPUT_COUNT;
        beeper.beep(config::BEEP_FIRE_HZ, config::BEEP_FIRE_MS);
        ESP_LOGI(TAG, "input -> %s", inputNames[activeInput]);
    });

    // Short click -> 1 s laser flash + fire chirp.
    encButton.onRelease([] {
        laser.flash();
        beeper.beep(config::BEEP_FIRE_HZ, config::BEEP_FIRE_MS);
        ESP_LOGI(TAG, "laser flash");
    });
    // Long press -> latch the laser on/off, with a higher tone for ON and a
    // lower tone for OFF.
    encButton.onLongPress([] {
        laser.toggleConstant();
        beeper.beep(laser.isConstant() ? config::BEEP_ON_HZ : config::BEEP_OFF_HZ,
                    config::BEEP_TOGGLE_MS);
        ESP_LOGI(TAG, "laser %s", laser.isConstant() ? "constant ON" : "off");
    });

    ESP_LOGI(TAG, "ready");

    bool     prevLaserOn = false;
    uint32_t lastUpdate  = 0;
    while (true)
    {
        // Poll the active input + debounce the buttons every (fast) iteration.
        inputs[activeInput]->poll();
        encButton.tick();
        bootButton.tick();
        laser.update();
        beeper.update();

        // Mirror the laser state on the onboard LED whenever it changes.
        if (laser.isOn() != prevLaserOn)
        {
            prevLaserOn = laser.isOn();
            setStatus(prevLaserOn ? config::LED_LASER : config::LED_IDLE);
        }

        // Update the gimbal target at the calmer UPDATE_PERIOD_MS rate
        // (see Config.hpp for why this is decoupled from the fast loop).
        uint32_t now = nowMs();
        if (now - lastUpdate >= config::UPDATE_PERIOD_MS)
        {
            lastUpdate = now;

            float x, y;
            inputs[activeInput]->read(x, y);
            gimbal.moveTo(x, y);

            // The logger decides whether this is worth printing.
            telemetry.update(x, y, panServo.angle(), tiltServo.angle(),
                             laser.isOn());
        }

        vTaskDelay(pdMS_TO_TICKS(config::LOOP_PERIOD_MS));
    }
}
