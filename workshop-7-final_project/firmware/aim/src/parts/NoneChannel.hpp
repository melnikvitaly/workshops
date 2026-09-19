#pragma once
#include <cstdint>
#include "IInputChannel.hpp"
#include "Gimbal.hpp"
#include "Point.hpp"

// No channel selected: hold the gimbal still. Exists so ctrl can dispatch
// through IInputChannel* uniformly instead of special-casing Channel::None.
class NoneChannel : public IInputChannel
{
public:
    explicit NoneChannel(Gimbal &gimbal) : _gimbal(gimbal) {}

    void update(uint32_t /*now*/, bool /*fresh*/) override
    {
        _gimbal.setVelocity({0.0f, 0.0f});
    }

    void reset(uint32_t /*now*/) override {}

private:
    Gimbal &_gimbal;
};
