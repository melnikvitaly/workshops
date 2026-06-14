#pragma once
#include <esp_log.h>
#include "Config.hpp"
#include "Input.hpp"
#include "Device.hpp"
#include "ViewPort.hpp"
#include "AppLogger.hpp"

// Drives the gimbal + laser from whichever Input is active. It owns the
// working-area ViewPort and the axis travel limits, and each update it executes
// the source's commands:
//
//   - the latest Move (sampled, not queued): hand the source's centred target
//     to the Gimbal, which translates it into absolute servo angles (ViewPort)
//     and drives the servos there;
//   - then each queued discrete action (laser + its audible/log feedback).
//
// This is the single place that knows how a source's coordinates reach the
// devices: it hands the centred target to the Gimbal (which translates it into
// servo angles via its ViewPort), so main.cpp's loop just calls update() each
// cycle (the active source comes from the Device).
class DeviceController
{
    static constexpr char TAG[] = "INPUT";

    static uint32_t nowMs() { return pdTICKS_TO_MS(xTaskGetTickCount()); }

    Device &_device;
    AppLogger &_appLogger;

    Point _target{0.0f, 0.0f};
    bool _prevLaserOn = false; // last laser state mirrored onto the status LED

    void executeInputCommand(const Command &c)
    {
        switch (c.type)
        {
        case CommandType::Move:
            _target = c.move;
            _device.gimbal.aim(c.move);
            break;
        case CommandType::LaserFlash:
            _device.laser.flash();
            _device.beeper.beep(config::BEEP_FIRE_HZ, config::BEEP_FIRE_MS);
            ESP_LOGI(TAG, "laser flash");
            break;
        case CommandType::LaserToggleConstant:
            _device.laser.toggleConstant();
            _device.beeper.beep(_device.laser.isConstant() ? config::BEEP_ON_HZ : config::BEEP_OFF_HZ,
                                config::BEEP_TOGGLE_MS);
            ESP_LOGI(TAG, "laser %s", _device.laser.isConstant() ? "constant ON" : "off");
            break;
        }
    }

    void wireControlsEvents()
    {
        _device.sourceButton.onRelease([this]
                                       {
            const char* name = _device.next();
            _device.beeper.beep(config::BEEP_FIRE_HZ, config::BEEP_FIRE_MS);
            ESP_LOGI(TAG, "input -> %s", name); });

        _device.controlButton.onRelease([this]
                                        { _device.activeInput().onEvent({InputEventType::ButtonClick}); });
        _device.controlButton.onLongPress([this]
                                          { _device.activeInput().onEvent({InputEventType::ButtonLongPress}); });
    }

public:
    DeviceController(Device &device, AppLogger &appLogger)
        : _device(device), _appLogger(appLogger)
    {
        wireControlsEvents();
    }

    void processInput()
    {
        Input &source = _device.activeInput();
        source.update();

        Command cmd;
        while (source.nextCommand(cmd))
            executeInputCommand(cmd);
    }

    void tick()
    {
        static uint32_t lastUpdate = 0;
        uint32_t now = nowMs();
        if (now - lastUpdate >= config::UPDATE_PERIOD_MS)
        {

            lastUpdate = now;

            processInput();
            postProcessStates();

            _appLogger.update(_target,
                              _device.gimbal.panAngle(), _device.gimbal.tiltAngle(),
                              _device.laser.isOn());
        }
    }

    void postProcessStates()
    {
        refreshStatusLedBasedOnLaser();
    }
    void refreshStatusLedBasedOnLaser()
    {
        bool on = _device.laser.isOn();
        if (on != _prevLaserOn)
        {
            _prevLaserOn = on;
            const config::Rgb &c = on ? config::LED_LASER : config::LED_IDLE;
            _device.status.rgb(c.r, c.g, c.b);
        }
    }
};
