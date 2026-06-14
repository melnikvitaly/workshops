#pragma once
#include "ADC.hpp"
#include "Sma.hpp"
#include "Config.hpp"

// Potentiometer = an ADC channel + a smoothing filter, exposed as a normalised
// position in the range [-1.0 .. +1.0] (centre of travel = 0.0).
//
// This is an *absolute* input: a given knob position always maps to the same
// output value (unlike the encoder, which is incremental).
class Potentiometer
{
    static constexpr float NORM_MIN = -1.0f;
    static constexpr float NORM_MAX =  1.0f;

    ADC&                            _adc;
    Sma<config::POT_FILTER_WINDOW> _sma;   // average raw samples to kill ADC jitter

public:
    explicit Potentiometer(ADC& adc) : _adc(adc) {}

    void init() { _adc.init(); }

    // Filtered raw reading (0 .. adc.maxRaw()).
    int readRaw() { return _sma.update(_adc.readRaw()); }

    // Normalised position in [-1, 1].
    float position()
    {
        float t = (float)readRaw() / (float)_adc.maxRaw();   // 0 .. 1
        return NORM_MIN + t * (NORM_MAX - NORM_MIN);         // -1 .. 1
    }
};
