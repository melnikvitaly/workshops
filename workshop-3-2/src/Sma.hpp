#pragma once

template<int N, typename T = int>
class Sma
{
    T   _buf[N] = {};
    int _idx    = 0;
    T   _sum    = 0;
    int _count  = 0;

public:
    T update(T val)
    {
        _sum -= _buf[_idx];
        _buf[_idx] = val;
        _sum += val;
        _idx = (_idx + 1) % N;
        if (_count < N) _count++;
        return _sum / _count;
    }
};
