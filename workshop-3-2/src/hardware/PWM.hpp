#pragma once
#include <driver/ledc.h>
#include <driver/gpio.h>

class PWM
{
    static constexpr uint32_t FREQ_HZ = 20000;
    static constexpr ledc_timer_bit_t RESOLUTION = LEDC_TIMER_10_BIT;
    static constexpr uint32_t MAX_DUTY = (1u << RESOLUTION) - 1;

    gpio_num_t _pin;
    ledc_channel_t _channel;
    ledc_timer_t _timer;
    uint32_t _duty = MAX_DUTY;
    bool _on = false;

    void applyDuty(uint32_t duty)
    {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, _channel, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, _channel);
    }

public:
    explicit PWM(gpio_num_t pin,
                 ledc_channel_t channel = LEDC_CHANNEL_0,
                 ledc_timer_t timer = LEDC_TIMER_0)
        : _pin(pin), _channel(channel), _timer(timer) {}

    void init()
    {
        ledc_timer_config_t timerCfg = {
            .speed_mode      = LEDC_LOW_SPEED_MODE,
            .duty_resolution = RESOLUTION,
            .timer_num       = _timer,
            .freq_hz         = FREQ_HZ,
            .clk_cfg         = LEDC_AUTO_CLK,
        };
        ledc_timer_config(&timerCfg);

        ledc_channel_config_t channelCfg = {
            .gpio_num   = (int)_pin,
            .speed_mode = LEDC_LOW_SPEED_MODE,
            .channel    = _channel,
            .intr_type  = LEDC_INTR_DISABLE,
            .timer_sel  = _timer,
            .duty       = 0,
            .hpoint     = 0,
        };
        ledc_channel_config(&channelCfg);
    }

    void on()  { _on = true;  applyDuty(_duty); }
    void off() { _on = false; applyDuty(0); }
    void max() { power(100); }

    void power(uint8_t percents)
    {
        if (percents > 100) percents = 100;
        uint32_t newDuty = (uint32_t)percents * MAX_DUTY / 100;
        if (newDuty == _duty && _on) return;
        _duty = newDuty;
        _on   = percents > 0;
        applyDuty(_duty);
    }
};
