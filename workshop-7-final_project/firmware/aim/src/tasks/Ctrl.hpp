#pragma once
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_timer.h>
#include <esp_log.h>

#include "Ipc.hpp"
#include "CmdQueue.hpp"
#include "LogQueue.hpp"
#include "Config.hpp"
#include "Pinout.hpp"
#include "StateMachine.hpp"
#include "Safety.hpp"
#include "ITransport.hpp"
#include "Ndjson.hpp"

#include "PWM.hpp"
#include "Servo.hpp"
#include "Relay.hpp"
#include "Gimbal.hpp"
#include "Laser.hpp"
#include "Pid.hpp"
#include "ZoneTour.hpp"
#include "ManualChannel.hpp"
#include "AutoChannel.hpp"

// The ctrl task. Owns the PIDs, the gimbal and the
// laser gate; the single consumer of cmd_q; the sole owner of the FSM. Runs a
// hard 20 ms step with vTaskDelayUntil.
//
// The control behaviour is the workshop-5 closed loop moved intact: per-axis
// PID on the camera error, deadzone hold, the 300 ms link failsafe that drops
// the integral, the boot zone tour, and live K-gain retune without an
// integrator reset.
class CtrlTask
{
public:
    explicit CtrlTask(Ipc &ipc) : _ipc(ipc) {}

    static void entry(void *arg) { static_cast<CtrlTask *>(arg)->run(); }

    // Hardware + config-derived setup. Runs in app_main, before the task starts.
    void init()
    {
        _gimbal.init();      // parks at the working-zone centre
        _laser.init();       // boot-safe: gate driven inactive before it is an output

        _ipc.config->snapshot(_cfg);
        _gimbal.setWorkingZone(_cfg.zone_pan_min, _cfg.zone_pan_max,
                               _cfg.zone_tilt_min, _cfg.zone_tilt_max);
        _zPanMin = _cfg.zone_pan_min; _zPanMax = _cfg.zone_pan_max;
        _zTiltMin = _cfg.zone_tilt_min; _zTiltMax = _cfg.zone_tilt_max;
        applyGains(true);
        _lastChannel = (config::Channel)_cfg.input_channel;

        const uint32_t now = nowMs();
        _autoChannel.reset(now); // primes _lastPidMs so the first frame's dt is sane
        _lastActivityMs   = now;
        _lastValidFrameMs = now - config::TRACK_TIMEOUT_MS - 1; // start stale
    }

    void run()
    {
        _sm.set(State::SelfTest, "boot");
        if (!selfTest())
        {
            _sm.set(State::Fault, "selftest.fail");
        }
        else if (config::ZONE_TOUR_AT_BOOT)
        {
            _sm.set(State::ZoneTour, "selftest.ok");
            _tour.begin();
        }
        else
        {
            _sm.set(State::ZoneTour, "selftest.ok");
            _sm.set(State::Disarmed, "tour.skip");
        }

        TickType_t last = xTaskGetTickCount();
        for (;;)
        {
            step();
            vTaskDelayUntil(&last, pdMS_TO_TICKS(config::UPDATE_PERIOD_MS));
        }
    }

private:
    static constexpr char TAG[] = "CTRL";

    static uint32_t nowMs() { return pdTICKS_TO_MS(xTaskGetTickCount()); }

    // --- one control step --------------------------------------------------
    void step()
    {
        const uint32_t now = nowMs();

        _ipc.config->snapshot(_cfg);
        applyConfigChanges(now);
        drainCmds(now);

        // E-stop: safety latched the atomic; ctrl turns it into the transition.
        if (_ipc.estopLatched.load(std::memory_order_relaxed) && _sm.state() != State::Fault)
        {
            _gimbal.stop();
            _autoChannel.resetPids();
            _sm.set(State::Fault, "estop");
            emitEstopEvt(now);
        }

        const bool chNone = (config::Channel)_cfg.input_channel == config::Channel::None;
        const bool fresh  = !chNone && (now - _lastValidFrameMs <= config::TRACK_TIMEOUT_MS);
        _ipc.linkFresh.store(fresh, std::memory_order_relaxed);

        if (stepState(now, fresh, chNone))
            _gimbal.update(config::UPDATE_PERIOD_S);
        updateLaser();
        pushLog(now);
    }

    // Task #10 (F-19) makes this a real gate: I2C scan, SD mount, servo sweep,
    // camera handshake. For task #2 it always passes.
    bool selfTest() { return true; }

    // --- config plane -> live state -------------------------------------------
    void applyConfigChanges(uint32_t now)
    {
        applyGains(false);

        if (_cfg.zone_pan_min != _zPanMin || _cfg.zone_pan_max != _zPanMax ||
            _cfg.zone_tilt_min != _zTiltMin || _cfg.zone_tilt_max != _zTiltMax)
        {
            _gimbal.setWorkingZone(_cfg.zone_pan_min, _cfg.zone_pan_max,
                                   _cfg.zone_tilt_min, _cfg.zone_tilt_max);
            _zPanMin = _cfg.zone_pan_min; _zPanMax = _cfg.zone_pan_max;
            _zTiltMin = _cfg.zone_tilt_min; _zTiltMax = _cfg.zone_tilt_max;
        }

        const config::Channel ch = (config::Channel)_cfg.input_channel;
        if (ch != _lastChannel)
        {
            // The handover reset - identical whether the change came from an
            // NDJSON cfg.set or the MODE button.
            resetLoop(now);
            _lastActivityMs = now;
            _lastChannel = ch;

            // A selected channel wakes the node out of PARKED.
            if (ch != config::Channel::None && _sm.state() == State::Parked)
                _sm.set(State::Disarmed, "cfg.channel");
        }
    }

    void applyGains(bool force)
    {
        if (force || _cfg.pid_pan_kp != _gPanKp || _cfg.pid_pan_ki != _gPanKi ||
            _cfg.pid_pan_kd != _gPanKd)
        {
            _autoChannel.setPanGains(_cfg.pid_pan_kp, _cfg.pid_pan_ki, _cfg.pid_pan_kd);
            _gPanKp = _cfg.pid_pan_kp; _gPanKi = _cfg.pid_pan_ki; _gPanKd = _cfg.pid_pan_kd;
        }
        if (force || _cfg.pid_tilt_kp != _gTiltKp || _cfg.pid_tilt_ki != _gTiltKi ||
            _cfg.pid_tilt_kd != _gTiltKd)
        {
            _autoChannel.setTiltGains(_cfg.pid_tilt_kp, _cfg.pid_tilt_ki, _cfg.pid_tilt_kd);
            _gTiltKp = _cfg.pid_tilt_kp; _gTiltKi = _cfg.pid_tilt_ki; _gTiltKd = _cfg.pid_tilt_kd;
        }
    }

    // --- cmd_q ------------------------------------------------------------------
    void drainCmds(uint32_t now)
    {
        CmdItem c;
        while (xQueueReceive(_ipc.cmdQ, &c, 0) == pdTRUE)
        {
            switch (c.kind)
            {
            case CmdKind::ErrorSample:
                if (c.flag)
                    _lastValidFrameMs = now;
                // Sign correction is the mounting, not the wire: it says how the
                // camera sits relative to the gimbal.
                _autoChannel.onErrorSample(c.flag,
                                           {config::PAN_INVERT ? -c.vec.x : c.vec.x,
                                            config::TILT_INVERT ? -c.vec.y : c.vec.y},
                                           c.t_ms);
                _lastActivityMs = now;
                break;

            case CmdKind::ManualVelocity:
                _manualChannel.set(c.vec, now);
                _lastValidFrameMs = now;
                _lastActivityMs   = now;
                break;

            case CmdKind::Nudge:
                _gimbal.nudge(c.vec);
                break;

            case CmdKind::FireLaser:
                if (laserPermitted(_ipc))
                {
                    _laser.fire();
                    ++_fireCount;
                }
                else
                {
                    emitLaserDenied(now);
                }
                break;

            case CmdKind::Arm:
                _lastActivityMs = now;
                handleArm(now);
                break;

            case CmdKind::FaultAck:
                _lastActivityMs = now;
                handleFaultAck(now);
                break;
            }
        }
    }

    void handleArm(uint32_t now)
    {
        switch (_sm.state())
        {
        case State::Disarmed:
        case State::Parked:
            resetLoop(now);
            _sm.set(State::Armed, "btn.control");
            break;
        case State::Armed:
        case State::LinkLost:
            _gimbal.stop();
            _sm.set(State::Disarmed, "btn.control");
            break;
        default:
            break; // BOOT / SELFTEST / ZONE_TOUR / FAULT ignore arm
        }
    }

    void handleFaultAck(uint32_t now)
    {
        if (_sm.state() != State::Fault)
            return;
        _ipc.estopLatched.store(false, std::memory_order_relaxed);
        _ipc.estopSource.store(EstopSource::None, std::memory_order_relaxed);
        resetLoop(now);
        _sm.set(State::Disarmed, "fault.ack");
    }

    // --- FSM: one case per state, dispatched once a tick ---------------------
    // Returns true if the gimbal's velocity integrator should run this tick.
    // ZONE_TOUR is the one state that drives position directly (Gimbal::moveTo
    // via ZoneTour::update) and must not have that integration layered on top.
    bool stepState(uint32_t now, bool fresh, bool chNone)
    {
        switch (_sm.state())
        {
        case State::Boot:
        case State::SelfTest:
            break; // only ever entered for one tick from run(), before the loop starts

        case State::ZoneTour:
            _tour.update(config::UPDATE_PERIOD_S);
            if (_tour.done())
            {
                resetLoop(now);
                _sm.set(State::Disarmed, "tour.done");
            }
            return false;

        case State::Disarmed:
            _gimbal.setVelocity({0.0f, 0.0f});
            if (chNone && (now - _lastActivityMs > config::PARK_IDLE_MS))
                _sm.set(State::Parked, "idle");
            break;

        case State::Armed:
            if (!fresh && !chNone)
            {
                _autoChannel.resetPids();
                _gimbal.stop();
                _sm.set(State::LinkLost, "link.stale");
            }
            else if (chNone && (now - _lastActivityMs > config::PARK_IDLE_MS))
            {
                _gimbal.stop();
                _sm.set(State::Parked, "idle");
            }
            else
            {
                computeMotion(now, fresh);
            }
            break;

        case State::LinkLost:
            _gimbal.setVelocity({0.0f, 0.0f});
            if (fresh)
            {
                resetLoop(now);
                _sm.set(State::Armed, "link.fresh");
            }
            break;

        case State::Parked:
            _gimbal.setVelocity({0.0f, 0.0f});
            break;

        case State::Fault:
            _gimbal.setVelocity({0.0f, 0.0f});
            break;
        }
        return true;
    }

    // --- motion (ARMED only - dispatched from stepState) ----------------------
    // `fresh` is guaranteed true whenever the Auto branch runs: stepState's
    // Armed case only reaches here when NOT(!fresh && !chNone), and the Auto
    // channel implies chNone is false.
    void computeMotion(uint32_t now, bool fresh)
    {
        switch ((config::Channel)_cfg.input_channel)
        {
        case config::Channel::Auto:
            _autoChannel.update(now, fresh);
            break;
        case config::Channel::Manual:
            _manualChannel.update(now);
            break;
        case config::Channel::None:
        default:
            _gimbal.setVelocity({0.0f, 0.0f});
            break;
        }
    }

    void resetLoop(uint32_t now)
    {
        _autoChannel.reset(now);
        _manualChannel.reset();
        _gimbal.setVelocity({0.0f, 0.0f});
    }

    // --- laser ------------------------------------------------------------------
    void updateLaser()
    {
        if (laserPermitted(_ipc))
            _laser.on();
        else
            _laser.off();
        _laser.tick();
    }

    // --- logging / events -------------------------------------------------------
    void pushLog(uint32_t now)
    {
        (void)now;
        LogRecord r{};
        r.kind      = LogRecord::Kind::Sample;
        r.seq       = _seq++;
        r.t_mono_us = (uint64_t)esp_timer_get_time();
        r.state     = _sm.state();
        r.channel   = _cfg.input_channel;
        const Point error = _autoChannel.error();
        r.ex        = error.x;
        r.ey        = error.y;
        const Point v = _gimbal.velocity();
        r.vpan      = v.x;
        r.vtilt     = v.y;
        r.pan       = _gimbal.panAngle();
        r.tilt      = _gimbal.tiltAngle();
        r.flags     = _autoChannel.onTarget() ? 1u : 0u;
        logSend(_ipc.logQ, &r, &_ipc.logDropped);

        _ipc.telem = {r.ex, r.ey, r.vpan, r.vtilt, r.pan, r.tilt};
    }

    static void onTransition(void *ctx, State from, State to, const char *why)
    {
        Ipc          *ipc = static_cast<Ipc *>(ctx);
        const uint64_t up = (uint64_t)esp_timer_get_time();

        char line[176];
        std::snprintf(line, sizeof(line),
                      "{\"t\":\"evt\",\"e\":\"state\",\"up\":%llu,\"from\":\"%s\",\"to\":\"%s\",\"why\":\"%s\"}",
                      (unsigned long long)up, stateName(from), stateName(to), why);
        ndjson::seal(line, sizeof(line));
        if (ipc->link)
            ipc->link->writeLine(line);
        ESP_LOGI("FSM", "%s -> %s (%s)", stateName(from), stateName(to), why);

        LogRecord r{};
        r.kind      = LogRecord::Kind::Transition;
        r.t_mono_us = up;
        r.state     = to;
        std::strncpy(r.note, why, sizeof(r.note) - 1);
        logSend(ipc->logQ, &r, &ipc->logDropped);
    }

    void emitEstopEvt(uint32_t now)
    {
        (void)now;
        const char *src = (_ipc.estopSource.load(std::memory_order_relaxed) == EstopSource::Uart)
                              ? "uart"
                              : "button";
        char line[128];
        std::snprintf(line, sizeof(line),
                      "{\"t\":\"evt\",\"e\":\"estop\",\"up\":%llu,\"src\":\"%s\",\"latched\":true}",
                      (unsigned long long)esp_timer_get_time(), src);
        ndjson::seal(line, sizeof(line));
        if (_ipc.link)
            _ipc.link->writeLine(line);
    }

    void emitLaserDenied(uint32_t now)
    {
        if (now - _lastDenyMs < 1000)
            return;
        _lastDenyMs = now;

        const char *why = _ipc.estopLatched.load(std::memory_order_relaxed) ? "estop"
                          : !_ipc.linkFresh.load(std::memory_order_relaxed)  ? "link_stale"
                                                                            : "state";
        char line[112];
        std::snprintf(line, sizeof(line),
                      "{\"t\":\"evt\",\"e\":\"laser_denied\",\"up\":%llu,\"why\":\"%s\"}",
                      (unsigned long long)esp_timer_get_time(), why);
        ndjson::seal(line, sizeof(line));
        if (_ipc.link)
            _ipc.link->writeLine(line);
    }

    // --- members ------------------------------------------------------------
    Ipc &_ipc;

    PWM _panPwm{pinout::SERVO_PAN, config::PAN_PWM_CHANNEL, config::PAN_PWM_TIMER,
                config::SERVO_FREQ_HZ, config::SERVO_PWM_RES};
    PWM _tiltPwm{pinout::SERVO_TILT, config::TILT_PWM_CHANNEL, config::TILT_PWM_TIMER,
                 config::SERVO_FREQ_HZ, config::SERVO_PWM_RES};
    Servo _panServo{_panPwm, config::SERVO_MIN_US, config::SERVO_MAX_US,
                    config::SERVO_MIN_ANGLE, config::SERVO_MAX_ANGLE};
    Servo _tiltServo{_tiltPwm, config::SERVO_MIN_US, config::SERVO_MAX_US,
                     config::SERVO_MIN_ANGLE, config::SERVO_MAX_ANGLE};
    Gimbal _gimbal{_panServo, _tiltServo, config::InitialViewPort,
                   config::GIMBAL_PAN_MIN, config::GIMBAL_PAN_MAX,
                   config::GIMBAL_TILT_MIN, config::GIMBAL_TILT_MAX,
                   config::SERVO_PAN_MAX_RATE, config::SERVO_TILT_MAX_RATE};

    Relay _laserRelay{pinout::LASER_GATE, config::RELAY_ACTIVE_HIGH};
    Laser _laser{_laserRelay, config::LASER_FIRE_BLANK_MS};

    AutoChannel _autoChannel{_gimbal,
                             config::PAN_KP, config::PAN_KI, config::PAN_KD, config::PAN_MAX_SLEW,
                             config::TILT_KP, config::TILT_KI, config::TILT_KD, config::TILT_MAX_SLEW,
                             config::PID_DERIV_ALPHA};
    ManualChannel _manualChannel{_gimbal};

    ZoneTour _tour{_gimbal, config::ZONE_TOUR_RATE_DEG_S, config::ZONE_TOUR_DWELL_MS,
                   config::PAN_ANGLE_AIMS_RIGHT, config::TILT_ANGLE_AIMS_DOWN};

    StateMachine _sm{_ipc.state, &CtrlTask::onTransition, &_ipc};

    config::ConfigBlob _cfg{config::CONFIG_DEFAULTS};
    config::Channel    _lastChannel = config::Channel::None;

    float _gPanKp = 0, _gPanKi = 0, _gPanKd = 0;
    float _gTiltKp = 0, _gTiltKi = 0, _gTiltKd = 0;
    float _zPanMin = 0, _zPanMax = 0, _zTiltMin = 0, _zTiltMax = 0;

    uint32_t _lastValidFrameMs = 0;
    uint32_t _lastActivityMs = 0;
    uint32_t _lastDenyMs     = 0;
    uint32_t _seq            = 0;
    uint32_t _fireCount      = 0;
};
