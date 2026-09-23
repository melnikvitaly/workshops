#pragma once
#include <cstdio>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_timer.h>
#include <esp_log.h>

#include "Ipc.hpp"
#include "CmdQueue.hpp"
#include "Config.hpp"
#include "ConfigStore.hpp"
#include "Pinout.hpp"
#include "ITransport.hpp"
#include "Ndjson.hpp"
#include "PerfStat.hpp"

#include "Button.h"
#include "StatusLed.hpp"
#include "Ssd1306.hpp"

// The ui task. Owns the OLED, the status LED and
// the two polled buttons. 50 Hz loop, OLED redrawn every 100 ms.
//
//   MODE  short press  -> next channel  NONE -> AUTO -> MANUAL -> NONE
//   MODE  long  >= 1 s -> NONE
//   CTRL  short press  -> arm/disarm, or acknowledge a latched FAULT
//
// The buttons are POLLED, never on an interrupt - channel selection is not
// realtime and the E-stop stays the only button on an ISR. The MODE button does
// not write configuration directly: it runs ConfigStore::set(), the same
// validate -> apply -> persist -> acknowledge path as an NDJSON cfg.set, then
// emits a cfg.state with src "button". ctrl performs the handover reset (both
// PIDs reset, velocity zeroed) when it sees the channel change in its snapshot.
class UiTask
{
public:
    explicit UiTask(Ipc &ipc) : _ipc(ipc) {}

    static void entry(void *arg) { static_cast<UiTask *>(arg)->run(); }

    void init()
    {
        _led.init();

        _oledOk = (_oled.init() == ESP_OK);
        if (!_oledOk)
            ESP_LOGW(TAG, "OLED absent or NAKing - degrading to LED + console, still controlling");

        _mode.init();
        _control.init();

        _mode.onRelease([this] { cycleChannel(config::nextChannel(_ipc.config->channel())); });
        _mode.onLongPress([this] { cycleChannel(config::Channel::None); });
        _control.onRelease([this] { controlPressed(); });
    }

    void run()
    {
        ESP_LOGI(TAG, "ui up (oled=%d)", (int)_oledOk);
        TickType_t last = xTaskGetTickCount();
        uint8_t    n    = 0;
        for (;;)
        {
            _mode.tick();
            _control.tick();
            refreshLed();
            if (++n >= RENDER_EVERY)
            {
                n = 0;
                render();
            }

            const uint32_t now = pdTICKS_TO_MS(xTaskGetTickCount());
            if (now - _lastPerfMs >= PERF_PERIOD_MS)
            {
                _lastPerfMs = now;
                reportTaskLoad();
            }

            vTaskDelayUntil(&last, pdMS_TO_TICKS(POLL_MS));
        }
    }

private:
    static constexpr char     TAG[]        = "UI";
    static constexpr uint32_t POLL_MS      = 20; // 50 Hz button poll
    static constexpr uint8_t  RENDER_EVERY = 5;  // -> 100 ms OLED refresh
    static constexpr uint8_t  BLINK_TICKS  = 8;  // 160 ms per LED blink phase
    static constexpr uint32_t PERF_PERIOD_MS = 1000; // CPU% / stack report period
    static constexpr int      LOAD_TASK_COUNT = 5;    // safety, ctrl, link_uart, logger, ui
    static constexpr int      MAX_SYSTEM_TASKS = 16;  // headroom over 5 app + 2 idle + system tasks

    // --- buttons -> actions ------------------------------------------------
    void cycleChannel(config::Channel next)
    {
        const ConfigStore::Result r =
            _ipc.config->set("input.channel", ConfigStore::Value::string(config::channelName(next)));
        if (!r.ok)
        {
            ESP_LOGW(TAG, "MODE channel set rejected: %s", r.err ? r.err : "?");
            return;
        }

        char vLit[10];
        std::snprintf(vLit, sizeof(vLit), "\"%s\"", config::channelName(next));
        char line[240];
        ndjson::cfgState(line, sizeof(line), "input.channel", vLit, 0, true, nullptr,
                         "button", (unsigned)config::SCHEMA_VERSION);
        _ipc.link->writeLine(line);

        // Local feedback for the case this button exists for - the link is down.
        startBlink(next);
        ESP_LOGI(TAG, "MODE -> channel %s", config::channelName(next));
    }

    void controlPressed()
    {
        CmdItem c{};
        c.t_ms = pdTICKS_TO_MS(xTaskGetTickCount());
        c.i    = 0;
        const State s = _ipc.state.load();
        if (s == State::Fault)
            c.kind = CmdKind::FaultAck;
        else if (s == State::Armed || s == State::LinkLost)
            c.kind = CmdKind::Disarm;
        else
            c.kind = CmdKind::Arm; // no-op in ctrl unless DISARMED/PARKED
        xQueueSend(_ipc.cmdQ, &c, pdMS_TO_TICKS(5));
    }

    // --- status LED --------------------------------------------------------
    void startBlink(config::Channel c)
    {
        const uint8_t ordinal = (uint8_t)c; // NONE=0, AUTO=1, MANUAL=2
        _blinkPhasesLeft = ordinal ? (uint8_t)(ordinal * 2) : 0;
        _blinkOn         = true;
        _blinkTickAcc    = 0;
        _blinkColour     = channelColour(c);
    }

    void refreshLed()
    {
        if (_blinkPhasesLeft > 0)
        {
            if (_blinkOn)
                _led.rgb(_blinkColour.r, _blinkColour.g, _blinkColour.b);
            else
                _led.off();
            if (++_blinkTickAcc >= BLINK_TICKS)
            {
                _blinkTickAcc = 0;
                _blinkOn      = !_blinkOn;
                --_blinkPhasesLeft;
            }
            return;
        }

        const config::Rgb c = stateColour(_ipc.state.load());
        _led.rgb(c.r, c.g, c.b);
    }

    static config::Rgb stateColour(State s)
    {
        switch (s)
        {
        case State::ZoneTour: return config::LED_TOUR;
        case State::Armed:    return config::LED_TRACKING;
        case State::LinkLost: return config::LED_LOST;
        case State::Fault:    return config::LED_FAULT;
        case State::Parked:   return config::LED_PARKED;
        case State::Disarmed: return config::LED_DISARMED;
        default:              return config::LED_PARKED; // BOOT / SELFTEST
        }
    }

    static config::Rgb channelColour(config::Channel c)
    {
        switch (c)
        {
        case config::Channel::Auto:   return {0, 20, 0};
        case config::Channel::Manual: return {0, 0, 24};
        default:                      return {12, 12, 12};
        }
    }

    // --- performance instrumentation -----------------------------------------
    //
    // uxTaskGetSystemState() needs every task's handle; ui is the one task
    // Ipc collects all five into (see Ipc.hpp), so it is the one that
    // computes this and publishes ipc.taskLoad for link_uart's tlm.sys.
    // Percentages are each task's share of *total* runtime-counter ticks
    // summed across both cores, so busy+idle across every task (including
    // both IDLE0/IDLE1) always sums to ~100% regardless of core count -
    // "idle" below is simply the remainder, not a named task lookup.
    void reportTaskLoad()
    {
        const TaskHandle_t handles[LOAD_TASK_COUNT] = {
            _ipc.safetyTask, _ipc.ctrlTask, _ipc.linkTask, _ipc.loggerTask, _ipc.uiTask,
        };
        for (TaskHandle_t h : handles)
            if (!h)
                return; // main.cpp has not finished publishing the handles yet

        static TaskStatus_t status[MAX_SYSTEM_TASKS];
        uint32_t             totalRuntime = 0;
        const UBaseType_t    n = uxTaskGetSystemState(status, MAX_SYSTEM_TASKS, &totalRuntime);

        static const char *NAMES[LOAD_TASK_COUNT] =
            {"safety", "ctrl", "link_uart", "logger", "ui"};

        uint32_t rt[LOAD_TASK_COUNT]  = {0};
        uint32_t hwm[LOAD_TASK_COUNT] = {0};
        for (UBaseType_t i = 0; i < n; ++i)
            for (int t = 0; t < LOAD_TASK_COUNT; ++t)
                if (status[i].xHandle == handles[t])
                {
                    rt[t]  = status[i].ulRunTimeCounter;
                    hwm[t] = (uint32_t)status[i].usStackHighWaterMark;
                }

        const uint32_t dTotal = totalRuntime - _prevTotalRt;
        _prevTotalRt = totalRuntime;

        float    pct[LOAD_TASK_COUNT];
        float    known  = 0.0f;
        uint32_t minHwm = UINT32_MAX;
        for (int t = 0; t < LOAD_TASK_COUNT; ++t)
        {
            const uint32_t d = rt[t] - _prevRt[t];
            _prevRt[t] = rt[t];
            pct[t]     = dTotal ? (100.0f * (float)d / (float)dTotal) : 0.0f;
            known += pct[t];
            if (hwm[t] < minHwm)
                minHwm = hwm[t];
            ESP_LOGI(TAG, "cpu %-9s %5.1f%%  stack free %lu words",
                     NAMES[t], (double)pct[t], (unsigned long)hwm[t]);
        }
        const float idle = known < 100.0f ? 100.0f - known : 0.0f;
        ESP_LOGI(TAG, "cpu %-9s %5.1f%%", "idle", (double)idle);

        // ctrl/logger/ui + idle only - safety and link_uart stay console-only
        // (both near-0% in normal operation), to leave the wire message's
        // 256-byte line cap room for the rest of it.
        _ipc.taskLoad.cpuCtrl       = pct[1];
        _ipc.taskLoad.cpuLogger     = pct[3];
        _ipc.taskLoad.cpuUi         = pct[4];
        _ipc.taskLoad.cpuIdle       = idle;
        _ipc.taskLoad.stackMinWords = (minHwm == UINT32_MAX) ? 0 : minHwm;
    }

    // --- OLED ------------------------------------------------------------------
    void render()
    {
        if (!_oledOk)
            return;
        ScopedPerf _perf(_ipc.perf.render);

        config::ConfigBlob c;
        _ipc.config->snapshot(c);

        const char *st = stateName(_ipc.state.load());
        const char *ch = config::channelName((config::Channel)c.input_channel);
        const uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);

        char l[24];
        _oled.clear();
        std::snprintf(l, sizeof(l), "AIM  %s", st);
        _oled.text(0, 0, l);
        std::snprintf(l, sizeof(l), "CH: %s", ch);
        _oled.text(0, 1, l);
        std::snprintf(l, sizeof(l), "CRC:%lu OVL:%lu",
                     (unsigned long)_ipc.badCrc.load(), (unsigned long)_ipc.overlong.load());
        _oled.text(0, 2, l);
        std::snprintf(l, sizeof(l), "UNP:%lu OOR:%lu",
                     (unsigned long)_ipc.unparsed.load(), (unsigned long)_ipc.outOfRange.load());
        _oled.text(0, 3, l);
        std::snprintf(l, sizeof(l), "DRPi:%lu ERR:%lu",
                     (unsigned long)_ipc.dropInactive.load(), (unsigned long)_ipc.uartErr.load());
        _oled.text(0, 4, l);
        std::snprintf(l, sizeof(l), "UP: %lus", (unsigned long)up);
        _oled.text(0, 5, l);

        char sdf[8];
        if (_ipc.sd.full)
            std::snprintf(sdf, sizeof(sdf), "FULL");
        else if (_ipc.sd.writeErrors)
            std::snprintf(sdf, sizeof(sdf), "E%lu",
                          (unsigned long)(_ipc.sd.writeErrors > 999 ? 999 : _ipc.sd.writeErrors));
        else
            std::snprintf(sdf, sizeof(sdf), "OK");
        const uint32_t freeMb = (uint32_t)(_ipc.sd.freeBytes >> 20);
        char sdl[40];
        std::snprintf(sdl, sizeof(sdl), "SD:%c%c %s %luM",
                      _ipc.sd.present ? 'P' : '-', _ipc.sd.mounted ? 'M' : '-',
                      sdf, (unsigned long)freeMb);
        _oled.text(0, 6, sdl);

        if (_oled.flush() != ESP_OK && _oledOk)
        {
            _oledOk = false;
            ESP_LOGW(TAG, "OLED write failed - disabling render, still controlling");
        }
    }

    Ipc      &_ipc;
    StatusLed _led{pinout::STATUS_LED};
    Ssd1306   _oled;
    bool      _oledOk = false;

    Button _mode{pinout::BTN_MODE, LOW, config::UI_DEBOUNCE_MS, config::UI_LONGPRESS_MS};
    Button _control{pinout::BTN_CONTROL, LOW, config::UI_DEBOUNCE_MS, config::UI_LONGPRESS_MS};

    uint8_t     _blinkPhasesLeft = 0;
    uint8_t     _blinkTickAcc    = 0;
    bool        _blinkOn         = false;
    config::Rgb _blinkColour{0, 0, 0};

    uint32_t _lastPerfMs   = 0;
    uint32_t _prevTotalRt  = 0;
    uint32_t _prevRt[LOAD_TASK_COUNT] = {0};
};
