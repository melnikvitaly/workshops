#pragma once

// Exponential moving average (program filter). Unlike the SMA it keeps no
// history buffer: each output blends the new sample with the previous output
// using a smoothing factor alpha in [0, 1]:
//
//   y[n] = alpha * x[n] + (1 - alpha) * y[n-1]
//
// Higher alpha -> follows the input faster (less smoothing); lower alpha ->
// heavier smoothing but more lag. alpha = 1 passes the input through unchanged.
template<typename T = float>
class Ema
{
    float _alpha;
    T     _value = 0;
    bool  _primed = false;   // seed with the first sample to avoid a slow ramp-up

public:
    explicit Ema(float alpha = 0.5f) : _alpha(alpha) {}

    T update(T val)
    {
        if (!_primed)
        {
            _value  = val;
            _primed = true;
        }
        else
        {
            _value = (T)(_alpha * val + (1.0f - _alpha) * _value);
        }
        return _value;
    }

    T    value() const { return _value; }
    void reset()       { _primed = false; _value = 0; }
};
