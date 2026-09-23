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
#include <esp_task_wdt.h>
#include <driver/gpio.h>

#include "Ipc.hpp"
#include "CmdQueue.hpp"
#include "LogQueue.hpp"
#include "Config.hpp"
#include "Pinout.hpp"
#include "StateMachine.hpp"
#include "Safety.hpp"
#include "ITransport.hpp"
#include "Ndjson.hpp"
#include "PerfStat.hpp"

#include "PWM.hpp"
#include "Servo.hpp"
#include "Relay.hpp"
#include "Gimbal.hpp"
#include "Laser.hpp"
#include "PositionalPid.hpp"
#include "Gains.hpp"
#include "Zone.hpp"
#include "ZoneTour.hpp"
#include "IInputChannel.hpp"
#include "ManualChannel.hpp"
#include "AutoChannel.hpp"
#include "AutoPositionChannel.hpp"
#include "NoneChannel.hpp"

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
        initScopePin(); // logic-analyser probe, toggled across step()

        _gimbal.init();      // parks at the working-zone centre
        _laser.init();       // boot-safe: gate driven inactive before it is an output

        _ipc.config->snapshot(_cfg);
        _zone = _cfg.zone;
        _gimbal.setWorkingZone(_zone);
        applyGains(true);
        _lastChannel   = (config::Channel)_cfg.input_channel;
        _activeChannel = selectChannel(_lastChannel);

        const uint32_t now = nowMs();
        _autoChannel.reset(now); // primes _lastPidMs so the first frame's dt is sane
        _lastActivityMs = now;
        _lastFrameMs    = now - config::TRACK_TIMEOUT_MS - 1; // start stale
    }

    void run()
    {
        // step() is non-blocking and runs every 20 ms, comfortably inside the
        // 1 s TWDT timeout - feed it right after step() below.
        ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));

        _sm.set(State::SelfTest, "boot");
        if (!selfTest())
        {
            _sm.set(State::Fault, "selftest.fail");
        }
        else if (_cfg.boot_tour)
        {
            _sm.set(State::ZoneTour, "selftest.ok");
            _tour.begin();
        }
        else
        {
            resetLoop(nowMs());
            _sm.set(State::Disarmed, "selftest.ok");
        }

        TickType_t last = xTaskGetTickCount();
        for (;;)
        {
            step();
            esp_task_wdt_reset();
            vTaskDelayUntil(&last, pdMS_TO_TICKS(config::UPDATE_PERIOD_MS));
        }
    }

private:
    static constexpr char TAG[] = "CTRL";

    static uint32_t nowMs() { return pdTICKS_TO_MS(xTaskGetTickCount()); }

    // SCOPE (GPIO47) is driven high for the duration of step() and nothing
    // else, so a logic analyser sees the control step's real wall-clock cost
    // - queue drains, the FSM dispatch and the PID all included - as one
    // pulse per 20 ms tick.
    static void initScopePin()
    {
        gpio_config_t io = {};
        io.pin_bit_mask = 1ULL << pinout::SCOPE;
        io.mode         = GPIO_MODE_OUTPUT;
        gpio_config(&io);
        gpio_set_level(pinout::SCOPE, 0);
    }

    // --- one control step --------------------------------------------------
    void step()
    {
        gpio_set_level(pinout::SCOPE, 1);
        ScopedPerf _perf(_ipc.perf.pidStep);

        const uint32_t now = nowMs();

        _ipc.config->snapshot(_cfg);
        applyConfigChanges(now);
        drainCmds(now);

        // E-stop: safety latched the atomic; ctrl turns it into the transition.
        if (_ipc.estopLatched.load() && _sm.state() != State::Fault)
        {
            _gimbal.stop();
            _activeChannel->reset(now);
            _sm.set(State::Fault, "estop");
            emitEstopEvt(now);
        }

        const bool chNone = (config::Channel)_cfg.input_channel == config::Channel::None;
        // "Fresh" is link liveness -- a frame arrived recently -- not target
        // visibility. valid=0 (dot lost this frame) must not read as silence:
        // the PC keeps sending at a steady rate specifically so a missed
        // detection holds the loop rather than tripping the link failsafe
        // (see serial_link.py's module docstring). Target visibility is a
        // separate concern, already handled inside AutoChannel::onErrorSample.
        const bool fresh = !chNone && (now - _lastFrameMs <= config::TRACK_TIMEOUT_MS);
        _ipc.linkFresh.store(fresh);

        if (stepState(now, fresh, chNone))
            _gimbal.update(config::UPDATE_PERIOD_S);
        updateLaser();
        _ipc.pidRuns.store(currentPidRuns());
        pushLog(now);

        gpio_set_level(pinout::SCOPE, 0);
    }

    // Task #10 (F-19) makes this a real gate: I2C scan, SD mount, servo sweep,
    // camera handshake. For task #2 it always passes.
    bool selfTest() { return true; }

    // --- config plane -> live state -------------------------------------------
    void applyConfigChanges(uint32_t now)
    {
        applyGains(false);

        if (_cfg.zone != _zone)
        {
            _gimbal.setWorkingZone(_cfg.zone);
            _zone = _cfg.zone;
        }

        const config::Channel ch = (config::Channel)_cfg.input_channel;
        if (ch != _lastChannel)
        {
            // The handover reset - identical whether the change came from an
            // NDJSON cfg.set or the MODE button.
            resetLoop(now);
            _activeChannel  = selectChannel(ch);
            _lastActivityMs = now;
            _lastChannel = ch;

            // A selected channel wakes the node out of PARKED.
            if (ch != config::Channel::None && _sm.state() == State::Parked)
                _sm.set(State::Disarmed, "cfg.channel");
        }
    }

    IInputChannel *selectChannel(config::Channel ch)
    {
        switch (ch)
        {
        case config::Channel::Auto:         return &_autoChannel;
        case config::Channel::Manual:       return &_manualChannel;
        case config::Channel::AutoPosition: return &_autoPositionChannel;
        case config::Channel::None:
        default:                            return &_noneChannel;
        }
    }

    void applyGains(bool force)
    {
        if (force || _cfg.pan_gains != _panGains)
        {
            _autoChannel.setPanGains(_cfg.pan_gains.kp, _cfg.pan_gains.ki, _cfg.pan_gains.kd);
            _panGains = _cfg.pan_gains;
        }
        if (force || _cfg.tilt_gains != _tiltGains)
        {
            _autoChannel.setTiltGains(_cfg.tilt_gains.kp, _cfg.tilt_gains.ki, _cfg.tilt_gains.kd);
            _tiltGains = _cfg.tilt_gains;
        }
        if (force || _cfg.pan_pos_gains != _panPosGains)
        {
            _autoPositionChannel.setPanGains(_cfg.pan_pos_gains.kp, _cfg.pan_pos_gains.ki, _cfg.pan_pos_gains.kd);
            _panPosGains = _cfg.pan_pos_gains;
        }
        if (force || _cfg.tilt_pos_gains != _tiltPosGains)
        {
            _autoPositionChannel.setTiltGains(_cfg.tilt_pos_gains.kp, _cfg.tilt_pos_gains.ki, _cfg.tilt_pos_gains.kd);
            _tiltPosGains = _cfg.tilt_pos_gains;
        }
    }

    // --- cmd_q ------------------------------------------------------------------
    // Handlers are the explicit specializations defined out of line, in a single
    // #pragma region, after the class. drainCmds() is defined out of line too, so
    // that by the time its body is parsed every specialization it calls has
    // already been declared - an explicit specialization must be visible before
    // any use that would otherwise implicitly instantiate the (undefined) primary.
    template <CmdKind K>
    void handleCmd(uint32_t now, const CmdItem &c);

    void drainCmds(uint32_t now);

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
                _activeChannel->reset(now);
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
                // fresh is guaranteed true here: the branch above already
                // catches !fresh && !chNone, so Auto (which implies !chNone)
                // only reaches this dispatch while fresh.
                _activeChannel->update(now, fresh);
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

    void resetLoop(uint32_t now)
    {
        _autoChannel.reset(now);
        _autoPositionChannel.reset(now);
        _manualChannel.reset(now);
        _gimbal.setVelocity({0.0f, 0.0f});
    }

    // --- telemetry source for whichever Auto-family channel is active ------
    // Only AUTO and AUTO_POS carry a meaningful error/on-target/pidRuns - both
    // expose the same accessor shape as a plain convention, not through
    // IInputChannel (which stays the minimal update()/reset() strategy
    // interface). Manual/None fall back to _autoChannel's (stale) values,
    // same as before this channel existed.
    Point currentError() const
    {
        return _lastChannel == config::Channel::AutoPosition
                   ? _autoPositionChannel.error()
                   : _autoChannel.error();
    }

    bool currentOnTarget() const
    {
        return _lastChannel == config::Channel::AutoPosition
                   ? _autoPositionChannel.onTarget()
                   : _autoChannel.onTarget();
    }

    uint32_t currentPidRuns() const
    {
        return _lastChannel == config::Channel::AutoPosition
                   ? _autoPositionChannel.pidRuns()
                   : _autoChannel.pidRuns();
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
        const Point error = currentError();
        r.ex        = error.x;
        r.ey        = error.y;
        const Point v = _gimbal.velocity();
        r.vpan      = v.x;
        r.vtilt     = v.y;
        r.pan       = _gimbal.panAngle();
        r.tilt      = _gimbal.tiltAngle();
        r.flags     = currentOnTarget() ? 1u : 0u;
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
        const char *src = (_ipc.estopSource.load() == EstopSource::Uart)
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

        const char *why = _ipc.estopLatched.load() ? "estop"
                          : !_ipc.linkFresh.load() ? "link_stale"
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
                   config::GIMBAL_MECH_ZONE,
                   config::SERVO_PAN_MAX_RATE, config::SERVO_TILT_MAX_RATE};

    Relay _laserRelay{pinout::LASER_GATE, config::RELAY_ACTIVE_HIGH};
    Laser _laser{_laserRelay, config::LASER_FIRE_BLANK_MS};

    AutoChannel _autoChannel{_gimbal,
                             config::PAN_KP, config::PAN_KI, config::PAN_KD, config::PAN_MAX_SLEW,
                             config::TILT_KP, config::TILT_KI, config::TILT_KD, config::TILT_MAX_SLEW,
                             config::PID_DERIV_ALPHA};
    AutoPositionChannel _autoPositionChannel{_gimbal,
                             config::PAN_POS_KP, config::PAN_POS_KI, config::PAN_POS_KD,
                             config::TILT_POS_KP, config::TILT_POS_KI, config::TILT_POS_KD,
                             config::PID_DERIV_ALPHA};
    ManualChannel _manualChannel{_gimbal};
    NoneChannel   _noneChannel{_gimbal};

    // The active IInputChannel* is kept in lockstep with _lastChannel by
    // selectChannel() - init() and applyConfigChanges() are the only writers.
    IInputChannel *_activeChannel = &_noneChannel;

    ZoneTour _tour{_gimbal, config::ZONE_TOUR_RATE_DEG_S, config::ZONE_TOUR_DWELL_MS,
                   config::PAN_ANGLE_AIMS_RIGHT, config::TILT_ANGLE_AIMS_DOWN};

    StateMachine _sm{_ipc.state, &CtrlTask::onTransition, &_ipc};

    config::ConfigBlob _cfg{config::CONFIG_DEFAULTS};
    config::Channel    _lastChannel = config::Channel::None;

    Gains _panGains{0, 0, 0};
    Gains _tiltGains{0, 0, 0};
    Gains _panPosGains{0, 0, 0};
    Gains _tiltPosGains{0, 0, 0};
    Zone _zone{0, 0, 0, 0};

    uint32_t _lastFrameMs = 0;    // last E/M frame of any validity -- link liveness
    uint32_t _lastActivityMs = 0;
    uint32_t _lastDenyMs     = 0;
    uint32_t _seq            = 0;
    uint32_t _fireCount      = 0;
};

// --- cmd_q handlers, one per CmdKind, single level of abstraction each -------
#pragma region CmdHandlers

template <>
inline void CtrlTask::handleCmd<CmdKind::ErrorSample>(uint32_t now, const CmdItem &c)
{
    // Any E frame proves the link is alive, valid=0 included -- see the
    // comment on _lastFrameMs's use in step(). Only a genuinely valid sample
    // updates the tracked error; onErrorSample() keeps that distinction.
    _lastFrameMs = now;
    // Sign correction is the mounting, not the wire: it says how the
    // camera sits relative to the gimbal.
    const Point err{config::PAN_INVERT ? -c.vec.x : c.vec.x,
                    config::TILT_INVERT ? -c.vec.y : c.vec.y};
    // link_uart only lets an E frame through while AUTO or AUTO_POS is
    // selected (see LinkUart.hpp), so _lastChannel alone picks the right one.
    if (_lastChannel == config::Channel::AutoPosition)
        _autoPositionChannel.onErrorSample(c.flag, err, c.t_ms);
    else
        _autoChannel.onErrorSample(c.flag, err, c.t_ms);
    _lastActivityMs = now;
}

template <>
inline void CtrlTask::handleCmd<CmdKind::ManualVelocity>(uint32_t now, const CmdItem &c)
{
    _manualChannel.set(c.vec, now);
    _lastFrameMs    = now;
    _lastActivityMs = now;
}

template <>
inline void CtrlTask::handleCmd<CmdKind::Nudge>(uint32_t /*now*/, const CmdItem &c)
{
    _gimbal.nudge(c.vec);
}

template <>
inline void CtrlTask::handleCmd<CmdKind::MoveTo>(uint32_t /*now*/, const CmdItem &c)
{
    _gimbal.moveTo(c.vec.x, c.vec.y);
}

template <>
inline void CtrlTask::handleCmd<CmdKind::FireLaser>(uint32_t now, const CmdItem & /*c*/)
{
    if (laserPermitted(_ipc))
    {
        _laser.fire();
        ++_fireCount;
    }
    else
    {
        emitLaserDenied(now);
    }
}

template <>
inline void CtrlTask::handleCmd<CmdKind::Arm>(uint32_t now, const CmdItem & /*c*/)
{
    _lastActivityMs = now;
    switch (_sm.state())
    {
    case State::Disarmed:
    case State::Parked:
        resetLoop(now);
        _sm.set(State::Armed, "btn.control");
        break;
    default:
        break; // already ARMED, or BOOT/SELFTEST/ZONE_TOUR/FAULT: no-op
    }
}

template <>
inline void CtrlTask::handleCmd<CmdKind::Disarm>(uint32_t now, const CmdItem & /*c*/)
{
    _lastActivityMs = now;
    switch (_sm.state())
    {
    case State::Armed:
    case State::LinkLost:
        _gimbal.stop();
        _sm.set(State::Disarmed, "btn.control");
        break;
    default:
        break; // already DISARMED, or BOOT/SELFTEST/ZONE_TOUR/FAULT: no-op
    }
}

template <>
inline void CtrlTask::handleCmd<CmdKind::FaultAck>(uint32_t now, const CmdItem & /*c*/)
{
    _lastActivityMs = now;
    if (_sm.state() != State::Fault)
        return;
    _ipc.estopLatched.store(false);
    _ipc.estopSource.store(EstopSource::None);
    resetLoop(now);
    _sm.set(State::Disarmed, "fault.ack");
}

template <>
inline void CtrlTask::handleCmd<CmdKind::ZoneTourStart>(uint32_t now, const CmdItem & /*c*/)
{
    // Only from an idle state: a tour started while ARMED/LINK_LOST would
    // hijack the gimbal from whatever is actively driving it.
    switch (_sm.state())
    {
    case State::Disarmed:
    case State::Parked:
        resetLoop(now);
        _tour.begin();
        _sm.set(State::ZoneTour, "cfg.tour");
        break;
    default:
        break;
    }
}

#pragma endregion

inline void CtrlTask::drainCmds(uint32_t now)
{    
#define AIM_CMD_CASE(K) \
    case CmdKind::K:    \
        handleCmd<CmdKind::K>(now, c); \
        break

    CmdItem c;
    while (xQueueReceive(_ipc.cmdQ, &c, 0) == pdTRUE)
    {
        switch (c.kind)
        {
        AIM_CMD_CASE(ErrorSample);
        AIM_CMD_CASE(ManualVelocity);
        AIM_CMD_CASE(Nudge);
        AIM_CMD_CASE(MoveTo);
        AIM_CMD_CASE(FireLaser);
        AIM_CMD_CASE(Arm);
        AIM_CMD_CASE(Disarm);
        AIM_CMD_CASE(FaultAck);
        AIM_CMD_CASE(ZoneTourStart);
        }
    }

#undef AIM_CMD_CASE
}
