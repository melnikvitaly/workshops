#pragma once
#include <esp_log.h>
#include "Pinout.hpp"
#include "Config.hpp"
#include "ViewPort.hpp"

#include "PWM.hpp"
#include "Servo.hpp"
#include "Relay.hpp"
#include "Buzzer.hpp"
#include "Button.h"
#include "StatusLed.hpp"

#include "Gimbal.hpp"
#include "Laser.hpp"
#include "Beeper.hpp"

#include "ErrorVectorInput.hpp"

/// @brief Collection of "parts", "drivers" and the single input source.
class Device
{
    static constexpr char TAG[] = "DEVICE";

    // --- owned hardware (low level) -----------------------------------------
    PWM _panPwm{pinout::SERVO_PAN, config::PAN_PWM_CHANNEL, config::PAN_PWM_TIMER,
                config::SERVO_FREQ_HZ, config::SERVO_PWM_RES};
    PWM _tiltPwm{pinout::SERVO_TILT, config::TILT_PWM_CHANNEL, config::TILT_PWM_TIMER,
                 config::SERVO_FREQ_HZ, config::SERVO_PWM_RES};
    Servo _panServo{_panPwm, config::SERVO_MIN_US, config::SERVO_MAX_US,
                    config::SERVO_MIN_ANGLE, config::SERVO_MAX_ANGLE};
    Servo _tiltServo{_tiltPwm, config::SERVO_MIN_US, config::SERVO_MAX_US,
                     config::SERVO_MIN_ANGLE, config::SERVO_MAX_ANGLE};

    Relay _laserRelay{pinout::RELAY, config::RELAY_ACTIVE_HIGH};

    PWM _buzzerPwm{pinout::BUZZER, config::BUZZER_PWM_CHANNEL, config::BUZZER_PWM_TIMER,
                   config::BEEP_FIRE_HZ, config::BUZZER_PWM_RES};
    Buzzer _buzzer{_buzzerPwm};

public:
    // --- controllable parts (driven by DeviceController) ---------------------
    Gimbal gimbal{_panServo, _tiltServo,
                  config::InitialViewPort,
                  config::GIMBAL_PAN_MIN, config::GIMBAL_PAN_MAX,
                  config::GIMBAL_TILT_MIN, config::GIMBAL_TILT_MAX,
                  config::SERVO_PAN_MAX_RATE, config::SERVO_TILT_MAX_RATE};
    Laser laser{_laserRelay, config::LASER_FIRE_BLANK_MS};
    Beeper beeper{_buzzer};
    StatusLed status{pinout::STATUS_LED};

    Button controlButton{pinout::CONTROL_BTN, LOW, config::BTN_DEBOUNCE_MS, config::BTN_LONG_PRESS_MS};
    Button armButton{pinout::BOOT_BTN, LOW, config::BTN_DEBOUNCE_MS, config::BTN_LONG_PRESS_MS};

    // The only input source. Held by concrete type (not Input&) so the
    // controller can read its tracking state for the status LED and logs.
    ErrorVectorInput tracking;

    void init()
    {
        laser.init();
        gimbal.init(); // parks at the centre of the working window
        tracking.init();
        beeper.init();
        status.init();

        controlButton.init();
        armButton.init();

        // The camera can only measure the error while the dot is visible, so
        // the laser latches on at boot and stays on.
        if (config::LASER_ON_AT_BOOT)
            laser.on();
    }

    void tick()
    {
        tracking.tick();
        controlButton.tick();
        armButton.tick();
        laser.tick();
        beeper.tick();
    }

    Input &activeInput() { return tracking; }
};
