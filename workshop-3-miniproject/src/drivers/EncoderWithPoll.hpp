#pragma once
#include <driver/gpio.h>
#include "Ema.hpp"

// Rotary (quadrature) encoder — rotation only.
// (The integrated push button is handled separately by the Button class.)
//
// ---------------------------------------------------------------------------
// HOW A QUADRATURE ENCODER WORKS  (the README asks to comment this in detail)
// ---------------------------------------------------------------------------
// The encoder has two switches, phase A and phase B, that open/close as the
// shaft turns. They are mechanically offset by a quarter period, so the two
// signals are 90 degrees out of phase. Reading them together gives a 2-bit
// number (A<<1 | B) that walks through a repeating "Gray code" sequence:
//
//      turning one way :  00 -> 01 -> 11 -> 10 -> 00 ...
//      turning the other:  00 -> 10 -> 11 -> 01 -> 00 ...
//
// Only ONE bit ever changes per step, which is what lets us tell direction:
// we remember the previous 2-bit state, and when it changes we look up
// (previous, current) in a table that says +1 (clockwise), -1 (counter-
// clockwise) or 0 (no movement / an illegal/bounced transition we ignore).
//
// Each mechanical "detent" (the click you feel) is usually 4 of these steps,
// so we divide the raw step count by stepsPerDetent to get clean detents.
//
// We use polling (poll() called from a fast loop) instead of interrupts so the
// logic stays easy to read; for a hand-turned knob this is plenty fast.
class EncoderWithPoll
{
    static constexpr int   DEFAULT_STEPS_PER_DETENT = 4;
    static constexpr float DEFAULT_SPAN_DETENTS     = 20.0f; // detents -> full deflection
    static constexpr float DEFAULT_FILTER_ALPHA     = 1.0f;  // 1.0 = no smoothing
    static constexpr float NORM_MIN                 = -1.0f;
    static constexpr float NORM_MAX                 =  1.0f;

    gpio_num_t _pinA;
    gpio_num_t _pinB;
    int        _stepsPerDetent;

    // How many detents from centre to a full +/-1 deflection (see position()).
    float _spanDetents;

    // Detents are discrete, so the raw normalised position jumps a whole step
    // at a time. An EMA eases position() toward each new detent so the gimbal
    // glides instead of snapping (alpha 1.0 passes the value through unchanged).
    Ema<float> _smooth;

    long    _steps     = 0;       // raw accumulated quadrature steps
    uint8_t _prevState = 0;       // last (A<<1 | B) value

    // Read both phases into a single 2-bit state value.
    uint8_t readState() const
    {
        return (uint8_t)((gpio_get_level(_pinA) << 1) | gpio_get_level(_pinB));
    }

public:
    EncoderWithPoll(gpio_num_t a, gpio_num_t b,
            float spanDetents    = DEFAULT_SPAN_DETENTS,
            int   stepsPerDetent = DEFAULT_STEPS_PER_DETENT,
            float filterAlpha    = DEFAULT_FILTER_ALPHA)
        : _pinA(a), _pinB(b), _stepsPerDetent(stepsPerDetent),
          _spanDetents(spanDetents), _smooth(filterAlpha) {}

    void init()
    {
        // Both phases are inputs with internal pull-ups, so an open contact
        // reads high and a closed one reads low.
        gpio_config_t io = {
            .pin_bit_mask = (1ULL << _pinA) | (1ULL << _pinB),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&io);

        _prevState = readState();
    }

    // Call this often (e.g. every 1-2 ms) so no steps are missed.
    void poll()
    {
        uint8_t state = readState();
        if (state != _prevState)
        {
            // Transition lookup table, indexed by (prev << 2) | current.
            // +1/-1 mark the valid clockwise / counter-clockwise transitions;
            // 0 marks "no change" or an impossible double-step (contact bounce)
            // which we deliberately discard.
            static const int8_t LUT[16] = {
                 0, -1, +1,  0,
                +1,  0,  0, -1,
                -1,  0,  0, +1,
                 0, +1, -1,  0,
            };
            _steps += LUT[(_prevState << 2) | state];
            _prevState = state;
        }
    }

    // Rotation in detents (signed, accumulates across calls).
    long count() const { return _steps / _stepsPerDetent; }

    // Normalised position in [-1, 1]: detent count scaled by the span and
    // clamped, so a full +/-_spanDetents turn maps to +/-1 (centre = 0), then
    // smoothed by the EMA. Advances the filter, so call it at a steady rate
    // (once per update cycle) — not const for that reason.
    float position()
    {
        float p = (float)count() / _spanDetents;
        if (p < NORM_MIN) p = NORM_MIN;
        if (p > NORM_MAX) p = NORM_MAX;
        return _smooth.update(p);
    }

    // Raw quadrature steps, mostly for debugging.
    long raw() const { return _steps; }

    void reset() { _steps = 0; }
};
