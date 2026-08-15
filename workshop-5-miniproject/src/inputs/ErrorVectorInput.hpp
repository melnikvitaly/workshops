#pragma once
#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "Input.hpp"
#include "Uart.hpp"
#include "Pid.hpp"
#include "Protocol.hpp"
#include "Config.hpp"
#include "Pinout.hpp"

// The one and only input source: a stream of error vectors arriving over UART
// from a PC running the camera.
//
// The PC sees both the laser dot and the target, computes
//
//     error = target - dot        (normalised to [-1, 1] across the frame)
//
// and streams it. That makes this a genuine closed loop: the measurement is
// independent of what we commanded, so everything the model cannot know -
// gravity droop on tilt, servo deadband, backlash, a horn that slipped - shows
// up in the error and gets corrected.
//
// The wire format lives in Protocol.hpp; this class is only concerned with what
// to *do* with a decoded frame. It runs one PID per axis and emits a VELOCITY
// command (deg/s); the Gimbal integrates it into an angle. See
// docs/uart-protocol.md for the full contract handed to whoever writes the PC
// end.
class ErrorVectorInput : public Input
{
    static constexpr char TAG[] = "TRACK";

    static uint32_t nowMs() { return pdTICKS_TO_MS(xTaskGetTickCount()); }

    Uart<protocol::MAX_LINE> _uart{pinout::LINK_UART, pinout::LINK_UART_BAUD};

    Pid _panPid{config::PAN_KP, config::PAN_KI, config::PAN_KD,
                -config::PAN_MAX_SLEW, config::PAN_MAX_SLEW,
                config::PID_DERIV_ALPHA};
    Pid _tiltPid{config::TILT_KP, config::TILT_KI, config::TILT_KD,
                 -config::TILT_MAX_SLEW, config::TILT_MAX_SLEW,
                 config::PID_DERIV_ALPHA};

    Point    _error{0.0f, 0.0f};    // latest reported error, already sign-corrected
    bool     _onTarget = false;     // current deadzone state, reported in telemetry
    Point    _velocity{0.0f, 0.0f};
    bool     _targetVisible = false;
    bool     _armed         = true;
    bool     _frameReady    = false; // an unprocessed frame is waiting
    uint32_t _frameMs       = 0;     // when that frame arrived
    uint32_t _lastFrameMs   = 0;     // when the last VALID frame arrived
    uint32_t _lastPidMs     = 0;     // when the PIDs last ran
    uint32_t _frameCount    = 0;
    uint32_t _rejectCount   = 0;
    uint32_t _fireCount     = 0;     // F frames honoured since boot

    // Inside the deadzone the axis is on target. Servos have 1-2 degrees of
    // deadband, so commanding ever-smaller rates there just makes the gimbal
    // hunt; report zero error instead, which also stops the integral growing.
    static float axisRate(Pid &pid, float error, float dt)
    {
        const float mag = error < 0.0f ? -error : error;
        if (mag < config::TRACK_DEADZONE)
        {
            pid.hold(error); // arrived: freeze, do not just zero the error
            return 0.0f;
        }
        return pid.update(error, dt);
    }

    // Stop the gimbal for this step. Advancing _lastPidMs matters: without it,
    // the dt handed to the PIDs on the first frame after a pause would span the
    // whole pause, and that one oversized step would dump a large chunk into
    // the integral just as tracking resumes.
    void holdStill(uint32_t now)
    {
        _velocity   = {0.0f, 0.0f};
        _lastPidMs  = now;
        _frameReady = false;
        _onTarget   = false; // re-arm: settling again is a fresh arrival
        enqueue(Command::setVelocity(_velocity));
    }

    // Retune live. Deliberately does NOT reset the integrators: the interesting
    // comparison is how each gain set behaves from the state you are already
    // in, and a reset would add a transient that belongs to the reset rather
    // than to the gains.
    void applyGains(const protocol::Frame &f)
    {
        if (f.axis != protocol::Axis::Tilt)
            _panPid.setGains(f.kp, f.ki, f.kd);
        if (f.axis != protocol::Axis::Pan)
            _tiltPid.setGains(f.kp, f.ki, f.kd);
        reportGains();
    }

    void reportGains()
    {
        printf("G pan %.2f %.2f %.2f tilt %.2f %.2f %.2f armed %d\n",
               _panPid.kp(), _panPid.ki(), _panPid.kd(),
               _tiltPid.kp(), _tiltPid.ki(), _tiltPid.kd(),
               _armed ? 1 : 0);
        fflush(stdout);
    }

    // Both axes inside the deadzone - the controllers are frozen and the gimbal
    // has stopped. AppLogger turns this state transition into telemetry arr:1.
    void updateArrival()
    {
        const float ax = _error.x < 0.0f ? -_error.x : _error.x;
        const float ay = _error.y < 0.0f ? -_error.y : _error.y;
        const bool  arrived = ax < config::TRACK_DEADZONE && ay < config::TRACK_DEADZONE;

        _onTarget = arrived;
    }

public:
    const char *name() const override { return "error-vector"; }

    void init() override
    {
        _uart.init();
        _lastPidMs = nowMs();
        ESP_LOGI(TAG, "listening for 'E <dx> <dy> <valid>' / 'F' on UART%d @ %d",
                 (int)pinout::LINK_UART, pinout::LINK_UART_BAUD);
    }

    // Fast loop: drain everything the PC has sent. Several frames can be
    // waiting, and only the freshest one is worth acting on - an older error is
    // simply a worse measurement of where the dot is now.
    void tick() override
    {
        char line[protocol::MAX_LINE];
        while (_uart.readLine(line, sizeof(line)))
        {
            const protocol::Frame f = protocol::parse(line);

            switch (f.type)
            {
            // Fire is a frame of its own rather than a field of the E frame on
            // purpose: E frames are measurements, freely dropped and superseded
            // below, and nothing droppable should be able to fire the laser.
            // Enqueued the moment it is decoded, so one frame is one flash even
            // if two arrive inside the same control step.
            case protocol::FrameType::Fire:
                ++_fireCount;
                enqueue(Command::action(CommandType::LaserFire));
                ESP_LOGI(TAG, "fire (from PC)");
                break;

            case protocol::FrameType::Error:
                ++_frameCount;
                _targetVisible = f.targetVisible;

                if (_targetVisible)
                {
                    // Sign correction is ours, not the protocol's: it describes
                    // how the camera is mounted relative to the gimbal.
                    _error = {config::PAN_INVERT ? -f.dx : f.dx,
                              config::TILT_INVERT ? -f.dy : f.dy};
                    _lastFrameMs = nowMs();
                }

                _frameMs    = nowMs();
                _frameReady = true;
                break;

            case protocol::FrameType::SetGains:
                applyGains(f);
                break;

            case protocol::FrameType::Nudge:
                // Straight through to the gimbal. The loop is not told about
                // it, which is the point: to the controller this looks exactly
                // like the world moving, i.e. a disturbance to reject.
                enqueue(Command::nudge({f.dx, f.dy}));
                break;

            case protocol::FrameType::Telemetry:
                enqueue(Command::action(CommandType::SetTelemetry, f.on ? 1 : 0));
                break;

            case protocol::FrameType::Query:
                reportGains();
                break;

            case protocol::FrameType::Invalid:
                ++_rejectCount;
                break;
            }
        }
    }

    // Control step. The PIDs run only when a fresh frame is available, with dt
    // measured from the previous run - NOT once per 20 ms tick. Ticking them
    // against a stale error would integrate the same value several times over
    // and show the derivative a false zero between frames.
    void update() override
    {
        const uint32_t now = nowMs();

        if (!_armed)
        {
            holdStill(now);
            return;
        }

        // Failsafe: the link died, or the PC stopped seeing anything. Stop, and
        // drop the PID state so a stale integral cannot kick the gimbal the
        // moment frames come back.
        if (now - _lastFrameMs > config::TRACK_TIMEOUT_MS)
        {
            _panPid.reset();
            _tiltPid.reset();
            holdStill(now);
            return;
        }

        // Target temporarily not visible: hold position, but keep the integral.
        // A brief occlusion should not throw away learned droop compensation.
        if (!_targetVisible)
        {
            holdStill(now);
            return;
        }

        if (!_frameReady)
            return; // nothing new; the Gimbal holds the last commanded rate

        float dt = (float)(_frameMs - _lastPidMs) / 1000.0f;
        if (dt < 0.001f)
            dt = 0.001f;
        if (dt > 0.5f)
            dt = 0.5f; // long gap: do not let one step dominate the integral
        _lastPidMs  = _frameMs;
        _frameReady = false;

        _velocity = {axisRate(_panPid, _error.x, dt),
                     axisRate(_tiltPid, _error.y, dt)};
        updateArrival();
        enqueue(Command::setVelocity(_velocity));
    }

    // Drop all loop state and restart the clock. Called when something else has
    // owned the gimbal for a while (the boot zone tour), so the first dt after
    // handover measures one control step rather than the whole interruption.
    void resetLoop()
    {
        _panPid.reset();
        _tiltPid.reset();
        _lastPidMs  = nowMs();
        _frameReady = false;
        _onTarget   = false;
    }

    bool onTarget() const { return _onTarget; }

    void onEvent(const InputEvent &e) override
    {
        if (e.type == InputEventType::ToggleArm)
        {
            _armed = !_armed;
            if (!_armed)
            {
                _panPid.reset();
                _tiltPid.reset();
            }
            _lastPidMs = nowMs();
            ESP_LOGI(TAG, "loop %s", _armed ? "ARMED" : "DISARMED");
            return;
        }
        Input::onEvent(e);
    }

    // --- state for logging / status ------------------------------------------
    bool     armed() const         { return _armed; }
    bool     targetVisible() const { return _targetVisible; }
    bool     linkAlive() const     { return nowMs() - _lastFrameMs <= config::TRACK_TIMEOUT_MS; }
    Point    error() const         { return _error; }
    Point    velocity() const      { return _velocity; }
    uint32_t frameCount() const    { return _frameCount; }
    uint32_t rejectCount() const   { return _rejectCount; }
    uint32_t fireCount() const     { return _fireCount; }
};
