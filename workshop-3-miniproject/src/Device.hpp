#pragma once
#include <esp_log.h>
#include "Pinout.hpp"
#include "Config.hpp"
#include "ViewPort.hpp"

#include "PWM.hpp"
#include "Servo.hpp"
#include "ADC.hpp"
#include "Relay.hpp"
#include "Buzzer.hpp"
#include "EncoderWithPoll.hpp"
#include "EncoderPcnt.hpp"
#include "Button.h"
#include "StatusLed.hpp"
#include "Potentiometer.hpp"

#include "Gimbal.hpp"
#include "Laser.hpp"
#include "Beeper.hpp"

#include "Input.hpp"
#include "ManualInput.hpp"
#include "AutoInput.hpp"
#include "JoystickInput.hpp"

/// @brief Collection of "parts", "driver" and "inputs"
class Device
{
    static constexpr char TAG[] = "DEVICE";
    static constexpr int COUNT = 3; // number of input sources
    
    using ActiveEncoder = EncoderPcnt;

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

    ADC _potAdc{pinout::POT_ADC_UNIT, pinout::POT_ADC_CHAN, config::POT_ADC_ATTEN};
    ActiveEncoder _encoder{pinout::ENC_A, pinout::ENC_B, config::ENC_SPAN_DETENTS,
                           config::ENC_STEPS_PER_DETENT, config::ENC_FILTER_ALPHA};
    Potentiometer _pot{_potAdc};

public:
    // --- controllable parts (driven by DeviceController) ---------------------
    Gimbal gimbal{_panServo, _tiltServo,
                  config::InitialViewPort,
                  config::GIMBAL_PAN_MIN, config::GIMBAL_PAN_MAX,
                  config::GIMBAL_TILT_MIN, config::GIMBAL_TILT_MAX};
    Laser laser{_laserRelay, config::LASER_FLASH_MS};
    Beeper beeper{_buzzer};
    StatusLed status{pinout::STATUS_LED};

    Button controlButton{pinout::ENC_SW, LOW, config::BTN_DEBOUNCE_MS, config::BTN_LONG_PRESS_MS};
    Button sourceButton{pinout::BOOT_BTN, LOW, config::BTN_DEBOUNCE_MS, config::BTN_LONG_PRESS_MS};

    explicit Device(int activeInput = INITIAL_INPUT_SOURCE)
        : _activeInput(activeInput) {}

    void init()
    {
        laser.init();
        gimbal.init();
        for (int i = 0; i < COUNT; ++i)
            _inputs[i]->init(); // each source inits its own hardware
        beeper.init();
        status.init();
        status.rgb(config::LED_IDLE.r, config::LED_IDLE.g, config::LED_IDLE.b);

        controlButton.init();
        sourceButton.init();
    }

    void tick()
    {
        activeInput().tick();
        controlButton.tick();
        sourceButton.tick();
        laser.tick();
        beeper.tick();
    }

    Input &activeInput() { return *_inputs[_activeInput]; }

    // Cycle to the next source; returns its name (for log/feedback).
    const char *next()
    {
        _activeInput = (_activeInput + 1) % COUNT;
        return _inputs[_activeInput]->name();
    }

private:
    // --- input sources ------------------------------------------------------
    // Sources emit unit [-1, 1] targets; the gimbal's ViewPort maps them to
    // angles, so no source needs to know the working area.
    // TODO: joystick needs a second ADC axis; it reuses `_pot` for both axes
    // until the Y-axis channel is wired up in Pinout.hpp.
    JoystickInput _joystickInput{_pot, _pot};
    ManualInput<ActiveEncoder> _manualInput{_pot, _encoder};
    AutoInput _autoInput{config::AUTO_CIRCLE_MS, config::AUTO_CROSS_MS};

    Input *_inputs[COUNT]{&_manualInput, &_autoInput, &_joystickInput};
    int _activeInput;
};
