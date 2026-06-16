#pragma once
#include <driver/pulse_cnt.h> // ESP-IDF 5.x hardware Pulse Counter driver
#include <esp_err.h>
#include "Ema.hpp"

class EncoderPcnt
{
    static constexpr int DEFAULT_STEPS_PER_DETENT = 4;
    static constexpr float DEFAULT_SPAN_DETENTS = 20.0f; // detents -> full deflection
    static constexpr float DEFAULT_FILTER_ALPHA = 1.0f;  // 1.0 = no smoothing
    static constexpr float NORM_MIN = -1.0f;
    static constexpr float NORM_MAX = 1.0f;

    // Counter limits: as wide as the 16-bit register allows (see note above).
    static constexpr int PCNT_HIGH_LIMIT = 32767;
    static constexpr int PCNT_LOW_LIMIT = -32768;

    static constexpr uint32_t GLITCH_FILTER_NS = 1000;

    gpio_num_t _pinA;
    gpio_num_t _pinB;
    int _stepsPerDetent;

    float _spanDetents;

    Ema<float> _smooth;

    pcnt_unit_handle_t _unit = nullptr;
    pcnt_channel_handle_t _chanA = nullptr;
    pcnt_channel_handle_t _chanB = nullptr;

public:
    EncoderPcnt(gpio_num_t a, gpio_num_t b,
                float spanDetents = DEFAULT_SPAN_DETENTS,
                int stepsPerDetent = DEFAULT_STEPS_PER_DETENT,
                float filterAlpha = DEFAULT_FILTER_ALPHA)
        : _pinA(a), _pinB(b), _stepsPerDetent(stepsPerDetent),
          _spanDetents(spanDetents), _smooth(filterAlpha) {}

    void init()
    {
        // 1) Create the unit. The driver enables internal pull-ups on the GPIOs
        //    when we attach channels, so an open contact reads high like before.
        pcnt_unit_config_t unitCfg = {
            .low_limit = PCNT_LOW_LIMIT,
            .high_limit = PCNT_HIGH_LIMIT,            
        };
        ESP_ERROR_CHECK(pcnt_new_unit(&unitCfg, &_unit));

        // 2) Glitch filter (hardware debounce of the rotation edges).
        pcnt_glitch_filter_config_t filterCfg = {
            .max_glitch_ns = GLITCH_FILTER_NS,
        };
        ESP_ERROR_CHECK(pcnt_unit_set_glitch_filter(_unit, &filterCfg));
        pcnt_chan_config_t chanACfg = {
            .edge_gpio_num = _pinA,
            .level_gpio_num = _pinB,
        };

        ESP_ERROR_CHECK(pcnt_new_channel(_unit, &chanACfg, &_chanA));

        pcnt_chan_config_t chanBCfg = {
            .edge_gpio_num = _pinB,
            .level_gpio_num = _pinA,
        };

        ESP_ERROR_CHECK(pcnt_new_channel(_unit, &chanBCfg, &_chanB));

        ESP_ERROR_CHECK(pcnt_channel_set_edge_action(
            _chanA, PCNT_CHANNEL_EDGE_ACTION_DECREASE,
            PCNT_CHANNEL_EDGE_ACTION_INCREASE));
        ESP_ERROR_CHECK(pcnt_channel_set_level_action(
            _chanA, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
            PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

        ESP_ERROR_CHECK(pcnt_channel_set_edge_action(
            _chanB, PCNT_CHANNEL_EDGE_ACTION_INCREASE,
            PCNT_CHANNEL_EDGE_ACTION_DECREASE));
        ESP_ERROR_CHECK(pcnt_channel_set_level_action(
            _chanB, PCNT_CHANNEL_LEVEL_ACTION_KEEP,
            PCNT_CHANNEL_LEVEL_ACTION_INVERSE));

        // 6) Enable, zero, and start counting.
        ESP_ERROR_CHECK(pcnt_unit_enable(_unit));
        ESP_ERROR_CHECK(pcnt_unit_clear_count(_unit));
        ESP_ERROR_CHECK(pcnt_unit_start(_unit));
    }

    // Kept for interface compatibility
    void poll() {}

    // Current raw quadrature steps straight from the PCNT register.
    long raw() const
    {
        int value = 0;
        pcnt_unit_get_count(_unit, &value);
        return value;
    }

    // Rotation in detents (signed, accumulates across calls).
    long count() const { return raw() / _stepsPerDetent; }

    float position()
    {
        float p = (float)count() / _spanDetents;
        if (p < NORM_MIN)
            p = NORM_MIN;
        if (p > NORM_MAX)
            p = NORM_MAX;
        return _smooth.update(p);
    }

    void reset()
    {
        pcnt_unit_clear_count(_unit);
        _smooth.reset();
    }
};
