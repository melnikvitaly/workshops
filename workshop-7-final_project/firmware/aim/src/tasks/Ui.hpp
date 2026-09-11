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

#include "Button.h"
#include "StatusLed.hpp"
#include "Ssd1306.hpp"

// The ui task (docs/architecture.md §2, §4). Owns the OLED, the status LED and
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
            vTaskDelayUntil(&last, pdMS_TO_TICKS(POLL_MS));
        }
    }

private:
    static constexpr char     TAG[]        = "UI";
    static constexpr uint32_t POLL_MS      = 20; // 50 Hz button poll
    static constexpr uint8_t  RENDER_EVERY = 5;  // -> 100 ms OLED refresh
    static constexpr uint8_t  BLINK_TICKS  = 8;  // 160 ms per LED blink phase

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
        c.kind = (_ipc.state.load(std::memory_order_relaxed) == State::Fault)
                     ? CmdKind::FaultAck
                     : CmdKind::Arm;
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

        const config::Rgb c = stateColour(_ipc.state.load(std::memory_order_relaxed));
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

    // --- OLED ------------------------------------------------------------------
    void render()
    {
        if (!_oledOk)
            return;

        config::ConfigBlob c;
        _ipc.config->snapshot(c);

        const char *st = stateName(_ipc.state.load(std::memory_order_relaxed));
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
};
