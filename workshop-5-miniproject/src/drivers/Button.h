#pragma once
#include <driver/gpio.h>
#include <esp_timer.h>
#include <functional>

// Debounced push button with press / release / long-press callbacks.
//
// Debounce / long-press durations are passed in via the constructor, and there
// is no interrupt path - this project polls via tick().
//
// Usage: register callbacks, then call tick() frequently to drive debouncing
// and long-press timing.
//
// Note: onRelease is *skipped* for the release that follows a long press, so a
// short press -> onRelease and a long press -> onLongPress are cleanly separated.

#ifndef LOW
constexpr int LOW = 0;
#endif
#ifndef HIGH
constexpr int HIGH = 1;
#endif

class Button
{
public:
    using Callback = std::function<void()>;

private:
    gpio_num_t _buttonPin;
    int _pressedPinState;
    int _lastPinState;
    int _pendingPinState = -1;
    uint32_t _pendingPinStateStarted = 0;
    uint32_t _debounceMs;
    uint32_t _longPressMs;
    uint32_t _longPressStartedAt = 0;
    bool _longPressFired = false;
    Callback _onPress;
    Callback _onRelease;
    Callback _onLongPressed;

    static uint32_t millis() { return (uint32_t)(esp_timer_get_time() / 1000); }

public:
    Button(gpio_num_t buttonPin, int pressedState,
           uint32_t debounceMs, uint32_t longPressMs)
        : _buttonPin(buttonPin),
          _pressedPinState(pressedState),
          _lastPinState(!pressedState),
          _debounceMs(debounceMs),
          _longPressMs(longPressMs)
    {
    }

    enum class BTN_CHANGE { Pressed, Released };

    void init()
    {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << _buttonPin),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = (_pressedPinState == LOW) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = (_pressedPinState != LOW) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
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

    void raiseLongPress()
    {
        if (_onLongPressed)
            _onLongPressed();
    }

    void tick()
    {
        int val = gpio_get_level(_buttonPin);

        if (val != _lastPinState)
        {
            if (val != _pendingPinState)
            {
                _pendingPinState = val;
                _pendingPinStateStarted = millis();
            }
        }
        else
        {
            _pendingPinState = -1;
            _pendingPinStateStarted = 0;
        }

        if (_pendingPinState > -1 && millis() - _pendingPinStateStarted > _debounceMs)
        {
            auto stateChange = (_pendingPinState == _pressedPinState)
                                   ? BTN_CHANGE::Pressed
                                   : BTN_CHANGE::Released;

            _lastPinState = _pendingPinState;
            _pendingPinState = -1;
            _pendingPinStateStarted = 0;

            if (stateChange == BTN_CHANGE::Released && _longPressFired)
            {
                _longPressFired = false;   // swallow the release that ends a long press
            }
            else
            {
                processEvent(stateChange);
            }

            if (stateChange == BTN_CHANGE::Pressed && _onLongPressed)
                _longPressStartedAt = millis();
            else
                _longPressStartedAt = 0;
        }

        if (_longPressStartedAt > 0 && millis() - _longPressStartedAt > _longPressMs)
        {
            raiseLongPress();
            _longPressFired = true;
            _longPressStartedAt = 0;
        }
    }
};
