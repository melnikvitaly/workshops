#pragma once
#include <driver/gpio.h>
#include <driver/ledc.h>
#include "Debug.hpp"

class PWM
{
    static constexpr uint32_t          FREQ_HZ    = 20000;
    static constexpr ledc_timer_bit_t  RESOLUTION = LEDC_TIMER_10_BIT;
    static constexpr uint32_t          MAX_DUTY   = (1u << RESOLUTION) - 1;

    gpio_num_t     _pin;
    ledc_channel_t _channel;
    ledc_timer_t   _timer;
    Debug          dbg;

    uint32_t _duty = MAX_DUTY;
    bool     _on   = false;

    void applyDuty(uint32_t duty) const
    {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, _channel, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, _channel);
    }

public:
    explicit PWM(gpio_num_t pin, Debug dbg,
                 ledc_channel_t channel = LEDC_CHANNEL_0,
                 ledc_timer_t timer = LEDC_TIMER_0)
        : _pin(pin), _channel(channel), _timer(timer), dbg(dbg) {}

    void init()
    {
        ledc_timer_config_t timerCfg = {
            .speed_mode      = LEDC_LOW_SPEED_MODE,
            .duty_resolution = RESOLUTION,
            .timer_num       = _timer,
            .freq_hz         = FREQ_HZ,
            .clk_cfg         = LEDC_AUTO_CLK,
            .deconfigure     = false,
        };
        ESP_ERROR_CHECK(ledc_timer_config(&timerCfg));

        // Zero-initialised, then filled field by field: ledc_channel_config_t
        // carries a deprecated intr_type member that a designated initialiser
        // would have to either name (deprecation warning) or skip (missing-field
        // warning), and both are errors in this build.
        ledc_channel_config_t channelCfg = {};
        channelCfg.gpio_num   = static_cast<int>(_pin);
        channelCfg.speed_mode = LEDC_LOW_SPEED_MODE;
        channelCfg.channel    = _channel;
        channelCfg.timer_sel  = _timer;
        channelCfg.duty       = 0;
        channelCfg.hpoint     = 0;
        channelCfg.sleep_mode = LEDC_SLEEP_MODE_NO_ALIVE_NO_PD;
        ESP_ERROR_CHECK(ledc_channel_config(&channelCfg));
    }

    void on()
    {
        _on = true;
        applyDuty(_duty);
    }

    void off()
    {
        _on = false;
        applyDuty(0);
    }

    void max() { power(100); }

    void power(uint8_t percents)
    {
        if (percents > 100)
            percents = 100;
        uint32_t newDuty = static_cast<uint32_t>(percents) * MAX_DUTY / 100;
        if (newDuty == _duty && _on)
            return;
        _duty = newDuty;
        _on   = percents > 0;
        applyDuty(_duty);
        dbg.print("PWM:%u%% duty=%lu", percents, static_cast<unsigned long>(_duty));
    }
};
