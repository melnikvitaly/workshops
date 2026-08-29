#pragma once
#include <functional>
#include <driver/gpio.h>
#include "Clock.hpp"
#include "Config.hpp"
#include "Debug.hpp"

// Polled, debounced button. The ISR path of the 2-3 version is dropped: a
// dedicated task polling every BUTTON_PERIOD_MS is the RTOS answer to "don't
// miss the press", and it keeps every callback out of interrupt context.
class Button
{
public:
    using Callback = std::function<void()>;

    enum class BTN_CHANGE
    {
        Pressed,
        Released
    };

private:
    gpio_num_t _buttonPin;
    int        _pressedPinState;
    int        _lastPinState;
    int        _pendingPinState        = -1;
    uint32_t   _pendingPinStateStarted = 0;
    uint32_t   _longPressStartedAt     = 0;
    bool       _longPressFired         = false;
    Debug      dbg;
    Callback   _onPress;
    Callback   _onRelease;
    Callback   _onLongPressed;

public:
    Button(gpio_num_t buttonPin, int pressedState, Debug dbg)
        : _buttonPin(buttonPin),
          _pressedPinState(pressedState),
          _lastPinState(!pressedState),
          dbg(dbg)
    {
    }

    void init()
    {
        gpio_config_t cfg = {
            .pin_bit_mask = 1ULL << _buttonPin,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = _pressedPinState == 0 ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = _pressedPinState == 0 ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&cfg));
        _lastPinState = gpio_get_level(_buttonPin);
    }

    void onPress(Callback cb) { _onPress = cb; }
    void onRelease(Callback cb) { _onRelease = cb; }
    void onLongPress(Callback cb) { _onLongPressed = cb; }

    void processEvent(BTN_CHANGE stateChange)
    {
        if (stateChange == BTN_CHANGE::Pressed)
        {
            if (_onPress)
                _onPress();
        }
        else if (stateChange == BTN_CHANGE::Released)
        {
            if (_onRelease)
                _onRelease();
        }
    }

    void dumpState(const char *msg) const
    {
        dbg.print("%s,%d,%d", msg, _lastPinState, _pendingPinState);
    }

    void raiseLongPress()
    {
        if (_onLongPressed)
            _onLongPressed();
    }

    void tick()
    {
        const int val = gpio_get_level(_buttonPin);

        if (val != _lastPinState)
        {
            if (val != _pendingPinState)
            {
                _pendingPinState        = val;
                _pendingPinStateStarted = nowMs();
                dumpState("pending");
            }
        }
        else
        {
            _pendingPinState        = -1;
            _pendingPinStateStarted = 0;
        }

        if (_pendingPinState > -1 && nowMs() - _pendingPinStateStarted > Config::DEBOUNCE_MS)
        {
            const auto stateChange = (_pendingPinState == _pressedPinState)
                                         ? BTN_CHANGE::Pressed
                                         : BTN_CHANGE::Released;

            dbg.dumpChange("lastPinState", _lastPinState, _pendingPinState);
            _lastPinState           = _pendingPinState;
            _pendingPinState        = -1;
            _pendingPinStateStarted = 0;

            if (stateChange == BTN_CHANGE::Released && _longPressFired)
            {
                _longPressFired = false;
                dumpState("Released skipped");
            }
            else
            {
                processEvent(stateChange);
            }

            _longPressStartedAt = (stateChange == BTN_CHANGE::Pressed && _onLongPressed)
                                      ? nowMs()
                                      : 0;
        }

        if (_longPressStartedAt > 0 && nowMs() - _longPressStartedAt > Config::LONG_PRESS_MS)
        {
            raiseLongPress();
            _longPressFired     = true;
            _longPressStartedAt = 0;
            dumpState("LongPress raised");
        }
    }
};
