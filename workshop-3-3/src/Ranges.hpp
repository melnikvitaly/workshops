#pragma once
#include <climits>

class Ranges
{
    int _min = INT_MAX;
    int _max = INT_MIN;

public:
    void update(int raw)
    {
        if (raw < _min) _min = raw;
        if (raw > _max) _max = raw;
    }

    int min() const { return _min == INT_MAX ? 0 : _min; }
    int max() const { return _max == INT_MIN ? 0 : _max; }
};
