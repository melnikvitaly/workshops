#pragma once

// A PID gain triple (kp, ki, kd). A plain aggregate with equality so ctrl can
// detect a config-plane change in one comparison instead of three.
struct Gains
{
    float kp, ki, kd;

    bool operator==(const Gains &o) const
    {
        return kp == o.kp && ki == o.ki && kd == o.kd;
    }
    bool operator!=(const Gains &o) const { return !(*this == o); }
};
