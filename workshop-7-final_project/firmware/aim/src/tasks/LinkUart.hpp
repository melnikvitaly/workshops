#pragma once
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_timer.h>
#include <esp_log.h>
#include <esp_system.h>

#include "Ipc.hpp"
#include "CmdQueue.hpp"
#include "Config.hpp"
#include "ConfigStore.hpp"
#include "Pinout.hpp"
#include "ITransport.hpp"
#include "Protocol.hpp"
#include "Ndjson.hpp"
#include "PerfStat.hpp"

// The link_uart task. Reads the EYE
// link, tells the two traffic classes apart by the first byte, produces cmd_q
// items, runs the config-plane acknowledge path, and emits tlm/evt/G lines.
//
// It is a producer for cmd_q and the emitter for cfg.state, tlm and G. State
// transition `evt` lines come from ctrl (it owns the FSM). Full receiver-counter
// telemetry and the 256-byte discard-and-count path are task #3.
class LinkUartTask
{
public:
    explicit LinkUartTask(Ipc &ipc) : _ipc(ipc) {}

    static void entry(void *arg) { static_cast<LinkUartTask *>(arg)->run(); }

    void run()
    {
        ESP_LOGI(TAG, "link_uart up on UART%d", (int)pinout::LINK_UART);
        char line[protocol::MAX_LINE];
        for (;;)
        {
            if (_ipc.link->readLine(line, sizeof(line))) // blocks up to ~10 ms when idle
            {
                ScopedPerf _perf(_ipc.perf.frameParse);
                handleLine(line);
            }
            maybeEmitTelemetry();
        }
    }

private:
    static constexpr char TAG[] = "LINK";

    static uint32_t nowMs() { return pdTICKS_TO_MS(xTaskGetTickCount()); }

    void handleLine(const char *line)
    {
        if (line[0] == '{')
            handleJson(line);
        else
            handleAscii(line);
    }

    // --- NDJSON config plane ------------------------------------------------
    void handleJson(const char *line)
    {
        const char *json = nullptr;
        size_t      jsonLen = 0;
        if (!ndjson::checkLine(line, &json, &jsonLen))
        {
            _ipc.badCrc.fetch_add(1);
            return;
        }

        char t[16];
        if (!ndjson::getStr(json, jsonLen, "t", t, sizeof(t)))
        {
            _ipc.unparsed.fetch_add(1);
            return;
        }
        const long id = ndjson::getInt(json, jsonLen, "id", 0);

        if (!std::strcmp(t, "cfg.set"))
            doCfgSet(json, jsonLen, id);
        else if (!std::strcmp(t, "cfg.get"))
            doCfgGet(json, jsonLen, id);
        else if (!std::strcmp(t, "cfg.reset"))
            doCfgReset(id);
        else if (!std::strcmp(t, "estop"))
            doEstop();
        else if (!std::strcmp(t, "arm"))
            doControlArm();
        else if (!std::strcmp(t, "disarm"))
            doControlDisarm();
        else
            _ipc.unparsed.fetch_add(1);
    }

    void doCfgSet(const char *json, size_t jsonLen, long id)
    {
        char key[32];
        if (!ndjson::getStr(json, jsonLen, "k", key, sizeof(key)))
        {
            _ipc.unparsed.fetch_add(1);
            return;
        }

        // fault.ack is an action, not stored config - it runs the same one path
        // as the CONTROL button.
        if (!std::strcmp(key, "fault.ack"))
        {
            postAction(CmdKind::FaultAck, nowMs());
            emitCfgState("fault.ack", "true", id, true, nullptr, "uart");
            return;
        }

        // control.zone_tour re-enters ZONE_TOUR on demand, so the working
        // zone set via zone.* can be watched without a reboot. No-ops
        // outside DISARMED/PARKED, same trade as arm/disarm: always acks
        // ok:true, and st: in telemetry is how the caller sees what happened.
        if (!std::strcmp(key, "control.zone_tour"))
        {
            postAction(CmdKind::ZoneTourStart, nowMs());
            emitCfgState("control.zone_tour", "true", id, true, nullptr, "uart");
            return;
        }

        const ndjson::Field vf = ndjson::find(json, jsonLen, "v");
        ConfigStore::Value  val;
        switch (vf.kind)
        {
        case ndjson::Field::Kind::String:
        {
            char vs[24]; // fits the longest channel name, AUTO_VELOCITYEQUATION (22 chars)
            const size_t n = (vf.len < sizeof(vs) - 1) ? vf.len : sizeof(vs) - 1;
            std::memcpy(vs, vf.ptr, n);
            vs[n] = '\0';
            val   = ConfigStore::Value::string(vs);
            break;
        }
        case ndjson::Field::Kind::Number:
            val = ConfigStore::Value::number(vf.num);
            break;
        case ndjson::Field::Kind::Bool:
            val = ConfigStore::Value::boolean(vf.flag);
            break;
        default:
            ESP_LOGW(TAG, "cfg.set %s rejected: type", key);
            emitCfgState(key, currentOrNull(key), id, false, "type", "uart");
            return;
        }

        const ConfigStore::Result r = _ipc.config->set(key, val);
        if (!r.ok && (!std::strcmp(r.err, "range") || !std::strcmp(r.err, "type")))
            _ipc.outOfRange.fetch_add(1);

        if (r.ok)
            ESP_LOGI(TAG, "cfg.set %s -> %s", key, currentOrNull(key));
        else
            ESP_LOGW(TAG, "cfg.set %s rejected: %s", key, r.err ? r.err : "?");

        emitCfgState(key, currentOrNull(key), id, r.ok, r.err, "uart");
    }

    void doCfgGet(const char *json, size_t jsonLen, long id)
    {
        char key[32];
        if (!ndjson::getStr(json, jsonLen, "k", key, sizeof(key)))
        {
            emitCfgState(nullptr, "null", id, false, "unknown_key", "uart");
            return;
        }
        emitCfgState(key, currentOrNull(key), id, true, nullptr, "uart");
    }

    void doCfgReset(long id)
    {
        const ConfigStore::Result r = _ipc.config->reset();
        emitCfgState(nullptr, "null", id, r.ok, r.err, "uart");
    }

    void doEstop()
    {
        _ipc.estopSource.store(EstopSource::Uart);
        xTaskNotifyGive(_ipc.safetyTask); // ctrl emits the evt when it sees the latch
    }

    // arm/disarm are their own top-level commands, not cfg.set keys: they
    // change nothing persisted, so there is nothing to validate or store.
    // Same shape as estop: no ack line, fire-and-forget -- the outcome is
    // already visible in the evt state-transition line and tlm's st field.
    // No-ops outside the matching state (see Ctrl.hpp handleCmd<Arm/Disarm>).
    void doControlArm()    { postAction(CmdKind::Arm, nowMs()); }
    void doControlDisarm() { postAction(CmdKind::Disarm, nowMs()); }

    // --- control-path ASCII -----------------------------------------------
    void handleAscii(const char *line)
    {
        const protocol::Frame f = protocol::parse(line);
        const config::Channel  ch = _ipc.config->channel();
        const uint32_t         now = nowMs();

        switch (f.type)
        {
        case protocol::FrameType::Error:
            // AUTO_POSITIONAL and AUTO_VELOCITYEQUATION are both
            // camera-error-driven - they differ in how ctrl turns the error
            // into a servo command, not in what feeds them.
            if (ch != config::Channel::AutoPositional && ch != config::Channel::AutoVelocityEquation)
            {
                _ipc.dropInactive.fetch_add(1);
                return;
            }
            postErr(now, {f.dx, f.dy}, f.targetVisible);
            break;

        case protocol::FrameType::ManualVel:
            if (ch != config::Channel::Manual)
            {
                _ipc.dropInactive.fetch_add(1);
                return;
            }
            postVec(CmdKind::ManualVelocity, now, {f.dx, f.dy});
            break;

        case protocol::FrameType::Fire:
            postAction(CmdKind::FireLaser, now);
            break;

        case protocol::FrameType::Nudge:
            postVec(CmdKind::Nudge, now, {f.dx, f.dy});
            break;

        case protocol::FrameType::Position:
            postVec(CmdKind::MoveTo, now, {f.dx, f.dy});
            break;

        case protocol::FrameType::SetGains:
            applyGains(f);
            emitGains();
            break;

        case protocol::FrameType::Telemetry:
            _telemetryOn = f.on;
            if (f.on)
                _lastTlmMs = 0;
            break;

        case protocol::FrameType::Query:
            emitGains();
            break;

        case protocol::FrameType::Invalid:
        default:
            if (f.reject == protocol::Reject::Range)
                _ipc.outOfRange.fetch_add(1);
            else
                _ipc.unparsed.fetch_add(1);
            break;
        }
    }

    void applyGains(const protocol::Frame &f)
    {
        // K persists through the one config path; ctrl picks the new gains up
        // on its next snapshot, no integrator reset.
        if (f.axis != protocol::Axis::Tilt)
        {
            _ipc.config->set("pid.pan.kp", ConfigStore::Value::number(f.kp));
            _ipc.config->set("pid.pan.ki", ConfigStore::Value::number(f.ki));
            _ipc.config->set("pid.pan.kd", ConfigStore::Value::number(f.kd));
        }
        if (f.axis != protocol::Axis::Pan)
        {
            _ipc.config->set("pid.tilt.kp", ConfigStore::Value::number(f.kp));
            _ipc.config->set("pid.tilt.ki", ConfigStore::Value::number(f.ki));
            _ipc.config->set("pid.tilt.kd", ConfigStore::Value::number(f.kd));
        }
    }

    // --- cmd_q producers -------------------------------------------------------
    void post(const CmdItem &c) { xQueueSend(_ipc.cmdQ, &c, pdMS_TO_TICKS(5)); }

    void postErr(uint32_t t, Point v, bool valid)
    {
        CmdItem c{};
        c.kind = CmdKind::ErrorSample;
        c.flag = valid;
        c.t_ms = t;
        c.vec  = v;
        post(c);
    }

    void postVec(CmdKind kind, uint32_t t, Point v)
    {
        CmdItem c{};
        c.kind = kind;
        c.t_ms = t;
        c.vec  = v;
        post(c);
    }

    void postAction(CmdKind kind, uint32_t t)
    {
        CmdItem c{};
        c.kind = kind;
        c.t_ms = t;
        c.i    = 0;
        post(c);
    }

    const char *currentOrNull(const char *key)
    {
        _ipc.config->format(key, _valBuf, sizeof(_valBuf));
        return _valBuf;
    }

    void emitCfgState(const char *key, const char *vLiteral, long id,
                      bool ok, const char *err, const char *src)
    {
        char line[240];
        ndjson::cfgState(line, sizeof(line), key, vLiteral, id, ok, err, src,
                         (unsigned)config::SCHEMA_VERSION);
        _ipc.link->writeLine(line);
    }

    void emitGains()
    {
        config::ConfigBlob c;
        _ipc.config->snapshot(c);
        // Report whichever channel is actually running: AUTO_VELOCITYEQUATION's
        // PID is a different gain set (velocity-equation form) from
        // AUTO_POSITIONAL's, and a `Q` while AUTO_VELOCITYEQUATION is active
        // should show what it is actually doing, not AUTO_POSITIONAL's
        // unrelated numbers.
        const bool veqActive = (config::Channel)c.input_channel == config::Channel::AutoVelocityEquation;
        const Gains &pan  = veqActive ? c.pan_veq_gains  : c.pan_gains;
        const Gains &tilt = veqActive ? c.tilt_veq_gains : c.tilt_gains;
        char line[128];
        std::snprintf(line, sizeof(line),
                      "G pan %.2f %.2f %.2f tilt %.2f %.2f %.2f armed %d",
                      (double)pan.kp, (double)pan.ki, (double)pan.kd,
                      (double)tilt.kp, (double)tilt.ki, (double)tilt.kd,
                      _ipc.state.load() == State::Armed ? 1 : 0);
        _ipc.link->writeLine(line);
    }

    void maybeEmitTelemetry()
    {
        if (!_telemetryOn)
            return;

        config::ConfigBlob c;
        _ipc.config->snapshot(c);
        const uint32_t now = nowMs();

        const uint32_t period = 1000u / (c.telemetry_rate_hz ? c.telemetry_rate_hz : 1);
        if (_lastTlmMs == 0 || now - _lastTlmMs >= period)
        {
            _lastTlmMs = now;
            emitTlm(c);
        }

        // tlm.sys (framing counters) and tlm.sd (storage health) both ride a
        // fixed 1 Hz, independent of telemetry.rate_hz.
        if (_lastSysMs == 0 || now - _lastSysMs >= SYS_PERIOD_MS)
        {
            _lastSysMs = now;
            emitTlmSys();
            emitTlmPerf();
        }
        if (_lastSdMs == 0 || now - _lastSdMs >= SYS_PERIOD_MS)
        {
            _lastSdMs = now;
            emitTlmSd();
        }
    }

    void emitTlm(const config::ConfigBlob &c)
    {
        const Ipc::TelemSample s = _ipc.telem; // lossy read, telemetry only
        char line[200];
        std::snprintf(line, sizeof(line),
                      "{\"t\":\"tlm\",\"up\":%llu,\"st\":\"%s\",\"ch\":\"%s\",\"ex\":%.3f,"
                      "\"ey\":%.3f,\"vp\":%.2f,\"vt\":%.2f,\"pan\":%.1f,\"tilt\":%.1f}",
                      (unsigned long long)esp_timer_get_time(),
                      stateName(_ipc.state.load()),
                      config::channelName((config::Channel)c.input_channel),
                      (double)s.ex, (double)s.ey, (double)s.vpan, (double)s.vtilt,
                      (double)s.pan, (double)s.tilt);
        ndjson::seal(line, sizeof(line));
        _ipc.link->writeLine(line);
    }

    void emitTlmSys()
    {
        // PID rate = evaluations since the last line / real elapsed time.
        const uint64_t nowUs = (uint64_t)esp_timer_get_time();
        const uint32_t runs  = _ipc.pidRuns.load();
        float pidHz = 0.0f;
        if (_lastPidUs != 0 && nowUs > _lastPidUs)
            pidHz = (float)(runs - _lastPidRuns) * 1e6f / (float)(nowUs - _lastPidUs);
        _lastPidUs   = nowUs;
        _lastPidRuns = runs;

        // Lossy read of ui's 1 Hz report - see Ipc::TaskLoad.
        const Ipc::TaskLoad &load = _ipc.taskLoad;

        char line[256];
        std::snprintf(line, sizeof(line),
                      "{\"t\":\"tlm.sys\",\"up\":%llu,\"link\":{\"bad_crc\":%lu,\"overlong\":%lu,"
                      "\"unparsed\":%lu,\"oor\":%lu,\"drop_inact\":%lu,\"uart_err\":%lu},"
                      "\"pid_hz\":%.1f,\"heap\":%lu,"
                      "\"cpu\":{\"ctrl\":%.0f,\"logger\":%.0f,\"ui\":%.0f,\"idle\":%.0f},"
                      "\"stack_min\":%lu,\"wdt\":0}",
                      (unsigned long long)nowUs,
                      (unsigned long)_ipc.badCrc.load(),
                      (unsigned long)_ipc.overlong.load(),
                      (unsigned long)_ipc.unparsed.load(),
                      (unsigned long)_ipc.outOfRange.load(),
                      (unsigned long)_ipc.dropInactive.load(),
                      (unsigned long)_ipc.uartErr.load(),
                      (double)pidHz,
                      (unsigned long)esp_get_free_heap_size(),
                      (double)load.cpuCtrl, (double)load.cpuLogger,
                      (double)load.cpuUi, (double)load.cpuIdle,
                      (unsigned long)load.stackMinWords);
        ndjson::seal(line, sizeof(line));
        _ipc.link->writeLine(line);
    }

    // Min/max/EWMA of the control step, one incoming line's parse, and one
    // OLED render - the three spans instrumented with esp_timer_get_time()
    // (see Ipc::Perf). A separate message rather than folding into tlm.sys
    // for the same reason tlm/tlm.sd/tlm.sys are already split: one object
    // carrying everything would not fit the 256-byte line cap.
    void emitTlmPerf()
    {
        const Ipc::Perf &p = _ipc.perf; // lossy read, telemetry only
        char line[220];
        std::snprintf(line, sizeof(line),
                      "{\"t\":\"tlm.perf\",\"up\":%llu,"
                      "\"pid\":{\"min\":%lu,\"max\":%lu,\"ema\":%lu},"
                      "\"parse\":{\"min\":%lu,\"max\":%lu,\"ema\":%lu},"
                      "\"render\":{\"min\":%lu,\"max\":%lu,\"ema\":%lu}}",
                      (unsigned long long)esp_timer_get_time(),
                      (unsigned long)p.pidStep.minUsOrZero(),
                      (unsigned long)p.pidStep.maxUs,
                      (unsigned long)p.pidStep.emaUs(),
                      (unsigned long)p.frameParse.minUsOrZero(),
                      (unsigned long)p.frameParse.maxUs,
                      (unsigned long)p.frameParse.emaUs(),
                      (unsigned long)p.render.minUsOrZero(),
                      (unsigned long)p.render.maxUs,
                      (unsigned long)p.render.emaUs());
        ndjson::seal(line, sizeof(line));
        _ipc.link->writeLine(line);
    }

    void emitTlmSd()
    {
        const Ipc::Sd s = _ipc.sd; // lossy read, telemetry only
        char line[240];
        std::snprintf(line, sizeof(line),
                      "{\"t\":\"tlm.sd\",\"up\":%llu,\"pres\":%u,\"mnt\":%u,\"full\":%u,"
                      "\"free\":%llu,\"werr\":%lu,\"drop\":%lu,\"qd\":%lu,\"bps\":%lu,"
                      "\"lmax\":%lu,\"lp95\":%lu}",
                      (unsigned long long)esp_timer_get_time(),
                      (unsigned)s.present, (unsigned)s.mounted, (unsigned)s.full,
                      (unsigned long long)s.freeBytes,
                      (unsigned long)s.writeErrors, (unsigned long)s.droppedRecords,
                      (unsigned long)s.queueDepth, (unsigned long)s.writeBytesPerS,
                      (unsigned long)s.writeMaxLatencyUs, (unsigned long)s.writeP95LatencyUs);
        ndjson::seal(line, sizeof(line));
        _ipc.link->writeLine(line);
    }

    static constexpr uint32_t SYS_PERIOD_MS = 1000;

    Ipc     &_ipc;
    bool     _telemetryOn = false;
    uint32_t _lastTlmMs   = 0;
    uint32_t _lastSysMs   = 0;
    uint32_t _lastSdMs    = 0;
    uint64_t _lastPidUs   = 0;
    uint32_t _lastPidRuns = 0;
    char     _valBuf[24]  = {0};
};
