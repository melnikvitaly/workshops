#pragma once
#include <esp_log.h>
#include "Config.hpp"
#include "Input.hpp"
#include "Device.hpp"
#include "AppLogger.hpp"
#include "TrackState.hpp"
#include "ZoneTour.hpp"

// Runs the control step.
//
// Every UPDATE_PERIOD_MS it lets the input source produce its commands, then
// executes them and advances the gimbal:
//
//   - SetVelocity: hand the pan/tilt rates to the Gimbal, which integrates them
//     into servo angles (so the Gimbal is the integrator of the control loop);
//   - the discrete laser actions and their audible feedback.
//
// The gimbal is stepped every cycle regardless of whether a new command
// arrived: the last commanded rate stays in effect between camera frames, which
// is what keeps motion smooth at 50 Hz while frames land at 15-30 Hz.
class DeviceController
{
    static constexpr char TAG[] = "CTRL";

    static uint32_t nowMs() { return pdTICKS_TO_MS(xTaskGetTickCount()); }

    Device    &_device;
    AppLogger &_appLogger;

    uint32_t   _lastUpdate = 0;
    TrackState _prevState  = TrackState::Disarmed;
    bool       _ledPrimed  = false;

    ZoneTour _tour{_device.gimbal, config::ZONE_TOUR_RATE_DEG_S, config::ZONE_TOUR_DWELL_MS,
                   config::PAN_ANGLE_AIMS_RIGHT, config::TILT_ANGLE_AIMS_DOWN};

    void executeInputCommand(const Command &c)
    {
        switch (c.type)
        {
        case CommandType::SetVelocity:
            _device.gimbal.setVelocity(c.velocity);
            break;
        case CommandType::LaserFire:
            _device.laser.fire();
            _device.beeper.beep(config::BEEP_FIRE_HZ, config::BEEP_FIRE_MS);
            break;
        case CommandType::Nudge:
            _device.gimbal.nudge(c.degrees);
            break;
        case CommandType::SetTelemetry:
            _appLogger.setTelemetry(c.value != 0);
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
        // BOOT button arms / disarms the loop. There is only one input source
        // now, so it no longer has anything to cycle.
        _device.armButton.onRelease([this]
                                    {
            _device.activeInput().onEvent({InputEventType::ToggleArm});
            _device.beeper.beep(_device.tracking.armed() ? config::BEEP_ON_HZ : config::BEEP_OFF_HZ,
                                config::BEEP_TOGGLE_MS); });

        _device.controlButton.onRelease([this]
                                        { _device.activeInput().onEvent({InputEventType::ButtonClick}); });
        _device.controlButton.onLongPress([this]
                                          { _device.activeInput().onEvent({InputEventType::ButtonLongPress}); });
    }

    // What the loop is doing right now - drives both the status LED and the log.
    TrackState state() const
    {
        if (!_tour.done())
            return TrackState::Touring;
        if (!_device.tracking.armed())
            return TrackState::Disarmed;
        if (!_device.tracking.linkAlive())
            return TrackState::NoLink;
        if (!_device.tracking.targetVisible())
            return TrackState::Lost;
        return TrackState::Tracking;
    }

    void refreshStatusLed(TrackState st)
    {
        if (_ledPrimed && st == _prevState)
            return;
        _prevState = st;
        _ledPrimed = true;

        const config::Rgb &c = (st == TrackState::Touring)  ? config::LED_TOUR
                               : (st == TrackState::Disarmed) ? config::LED_DISARMED
                               : (st == TrackState::Tracking) ? config::LED_TRACKING
                                                              : config::LED_LOST;
        _device.status.rgb(c.r, c.g, c.b);
    }

    // Execute whatever the source has queued. With applyMotion == false the
    // velocity commands are dropped rather than left in the queue: during the
    // tour the gimbal is not ours to steer, and letting them pile up would just
    // apply a stale rate the moment the tour ended.
    void drainCommands(bool applyMotion)
    {
        Command cmd;
        while (_device.activeInput().nextCommand(cmd))
        {
            if (!applyMotion && cmd.type == CommandType::SetVelocity)
                continue;
            executeInputCommand(cmd);
        }
    }

    void processInput()
    {
        _device.activeInput().update();
        drainCommands(true);
    }

public:
    DeviceController(Device &device, AppLogger &appLogger)
        : _device(device), _appLogger(appLogger)
    {
        wireControlsEvents();
    }

    // Call once after Device::init(). Kicks off the boot zone tour, which owns
    // the gimbal until it finishes.
    void begin()
    {
        if (config::ZONE_TOUR_AT_BOOT)
        {
            ESP_LOGI(TAG, "zone tour: pan %.0f..%.0f, tilt %.0f..%.0f "
                          "- clockwise from top-left; if it runs the other way, "
                          "an axis-geometry flag is wrong",
                     config::WORK_PAN_MIN, config::WORK_PAN_MAX,
                     config::WORK_TILT_MIN, config::WORK_TILT_MAX);
            _tour.begin();
        }
    }

    void tick()
    {
        const uint32_t now = nowMs();
        if (now - _lastUpdate < config::UPDATE_PERIOD_MS)
            return;
        _lastUpdate = now;

        // The tour owns the gimbal at boot. Buttons and F frames still work -
        // their commands are executed - but motion commands are discarded, and
        // the input's update() is not called at all, so the PIDs stay idle
        // rather than integrating against a gimbal they are not driving.
        if (!_tour.done())
        {
            drainCommands(false);
            _tour.update(config::UPDATE_PERIOD_S);
            refreshStatusLed(state());

            if (_tour.done())
            {
                // The tour held the gimbal for seconds; without this the first
                // control step would measure dt across the whole tour.
                _device.tracking.resetLoop();
                ESP_LOGI(TAG, "zone tour complete - loop live");
            }
            return;
        }

        processInput();
        _device.gimbal.update(config::UPDATE_PERIOD_S);

        const TrackState st = state();
        refreshStatusLed(st);

        _appLogger.update(now,
                          _device.tracking.error(),
                          _device.gimbal.velocity(),
                          _device.gimbal.panAngle(), _device.gimbal.tiltAngle(),
                          st, _device.tracking.onTarget());
    }
};
