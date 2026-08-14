#pragma once
#include <cstdint>

// Fixed-capacity FIFO of T, sized for the handful of items that can pile up
// between two reads (no heap, safe to embed anywhere). Producers push(); the
// consumer drains with pop().
//
// If the queue is full an incoming item overwrites the oldest one: dropping a
// stale item is better than blocking the producer or growing without bound.
template <typename T, int Capacity = 8>
class Queue
{
    T       _buf[Capacity];
    uint8_t _head  = 0;   // index of the oldest queued item
    uint8_t _count = 0;   // number currently queued

public:
    bool empty() const { return _count == 0; }

    void push(const T& v)
    {
        if (_count == Capacity)            // full: drop the oldest to make room
            _head = (_head + 1) % Capacity;
        else
            ++_count;
        _buf[(_head + _count - 1) % Capacity] = v;
    }

    // Pop the oldest item into out; false (out untouched) if empty.
    bool pop(T& out)
    {
        if (_count == 0)
            return false;
        out   = _buf[_head];
        _head = (_head + 1) % Capacity;
        --_count;
        return true;
    }
};
