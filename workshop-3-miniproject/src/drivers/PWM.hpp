#pragma once
#include <driver/ledc.h>
#include <driver/gpio.h>

// Generic LEDC-based PWM channel.
//
// Compared to the earlier workshops this version keeps the resolution and
// frequency configurable per-instance so the same class can drive both a
// "fast" load (LED / relay coil, ~1 kHz, coarse 10-bit duty) and a hobby
// servo (50 Hz, fine 14-bit duty addressed in microseconds).
class PWM
{
    static constexpr uint8_t  PERCENT_MAX     = 100;
    static constexpr uint32_t DEFAULT_FREQ_HZ = 1000;
    static constexpr uint64_t US_PER_SEC      = 1000000ULL;

    gpio_num_t       _pin;
    ledc_channel_t   _channel;
    ledc_timer_t     _timer;
    uint32_t         _freqHz;
    ledc_timer_bit_t _resolution;
    uint32_t         _maxDuty;          // full-scale duty count (2^bits - 1)
    uint32_t         _duty;             // last duty applied while "on"
    bool             _on = false;

    void applyDuty(uint32_t duty)
    {
        ledc_set_duty(LEDC_LOW_SPEED_MODE, _channel, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, _channel);
    }

public:
    explicit PWM(gpio_num_t pin,
                 ledc_channel_t channel = LEDC_CHANNEL_0,
                 ledc_timer_t timer = LEDC_TIMER_0,
                 uint32_t freqHz = DEFAULT_FREQ_HZ,
                 ledc_timer_bit_t resolution = LEDC_TIMER_10_BIT)
        : _pin(pin), _channel(channel), _timer(timer), _freqHz(freqHz),
          _resolution(resolution), _maxDuty((1u << resolution) - 1),
          _duty(_maxDuty) {}

    void init()
    {
        ledc_timer_config_t timerCfg = {
            .speed_mode      = LEDC_LOW_SPEED_MODE,
            .duty_resolution = _resolution,
            .timer_num       = _timer,
            .freq_hz         = _freqHz,
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

    void setFreq(uint32_t hz)
    {
        _freqHz = hz;
        ledc_set_freq(LEDC_LOW_SPEED_MODE, _timer, hz);
    }

    void on()  { _on = true;  applyDuty(_duty); }
    void off() { _on = false; applyDuty(0); }

    // Duty as a 0..100 percentage (handy for LED / relay).
    void power(uint8_t percents)
    {
        if (percents > PERCENT_MAX) percents = PERCENT_MAX;
        uint32_t newDuty = (uint32_t)percents * _maxDuty / PERCENT_MAX;
        if (newDuty == _duty && _on) return;
        _duty = newDuty;
        _on   = percents > 0;
        applyDuty(_duty);
    }

    // Set the raw duty count directly (0.._maxDuty).
    void writeDuty(uint32_t duty)
    {
        if (duty > _maxDuty) duty = _maxDuty;
        _duty = duty;
        _on   = duty > 0;
        applyDuty(_duty);
    }

    // Set the pulse width in microseconds. This is the natural way to talk to
    // a servo: duty_count = pulse / period * full_scale, where period = 1/freq.
    // 64-bit math avoids overflow for high resolutions.
    void writeMicroseconds(uint32_t us)
    {
        uint64_t fullScale = (uint64_t)_maxDuty + 1;
        uint32_t duty = (uint32_t)((uint64_t)us * _freqHz * fullScale / US_PER_SEC);
        writeDuty(duty);
    }

    uint32_t maxDuty() const { return _maxDuty; }
};
