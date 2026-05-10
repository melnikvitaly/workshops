#pragma once
#include <driver/gpio.h>
#include <esp_timer.h>
#include <hardware/Debug.h>
#include <Config.h>
#include <functional>
#include <string>

constexpr int LOW = 0;
constexpr int HIGH = 1;

class Button
{
public:
    using Callback = std::function<void()>;

private:
    uint8_t _buttonPin;
    int _pressedPinState;
    int _lastPinState;
    volatile int _pendingPinState = -1;
    volatile uint32_t _pendingPinStateStarted;
    uint32_t _longPressStartedAt;
    bool _longPressFired;
    Debug &dbg;
    Callback _onPress;
    Callback _onRelease;
    Callback _onLongPressed;
    bool _useISR = false;
    volatile bool _isrPressed = false;

    static uint32_t millis() { return (uint32_t)(esp_timer_get_time() / 1000); }

public:
    Button(uint8_t buttonPin, int pressedState, bool useISR, Debug &dbg)
        : _buttonPin(buttonPin),
          _pressedPinState(pressedState),
          _useISR(useISR),
          _lastPinState(!pressedState),
          _longPressStartedAt(0),
          _longPressFired(false),
          dbg(dbg)
    {
    }

    enum class BTN_CHANGE
    {
        Pressed,
        Released
    };

    void init()
    {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << _buttonPin),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = (_pressedPinState == LOW) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = (_pressedPinState != LOW) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
            .intr_type = _useISR ? GPIO_INTR_ANYEDGE : GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        _lastPinState = gpio_get_level((gpio_num_t)_buttonPin);
        if (_useISR)
        {
            attachInterrupts();
        }
    }

    void onPress(Callback cb) { _onPress = cb; }
    void onRelease(Callback cb) { _onRelease = cb; }
    void onLongPress(Callback cb) { _onLongPressed = cb; }

    void onIsrChange()
    {
        _isrPressed = gpio_get_level((gpio_num_t)_buttonPin);
    }

    static void IRAM_ATTR changeIsr(void *arg)
    {
        Button *btn = (Button *)arg;
        btn->onIsrChange();
    }

    void attachInterrupts()
    {
        // safe to call multiple times; returns ESP_ERR_INVALID_STATE if already installed
        gpio_install_isr_service(0);
        gpio_isr_handler_add((gpio_num_t)_buttonPin, changeIsr, (void *)this);
    }

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

    void dumpState(const std::string &msg)
    {
        dbg.print(msg + "," + std::to_string(_lastPinState) + "," + std::to_string(_pendingPinState));
    }

    void raiseLongPress()
    {
        if (_onLongPressed)
            _onLongPressed();
    }

    int readNewValue()
    {
        if (_useISR)
            return _isrPressed ? 1 : 0;
        return gpio_get_level((gpio_num_t)_buttonPin);
    }

    void tick()
    {
        int val = readNewValue();

        if (val != _lastPinState)
        {
            if (val != _pendingPinState)
            {
                _pendingPinState = val;
                _pendingPinStateStarted = millis();
                dumpState("pending");
            }
        }
        else
        {
            _pendingPinState = -1;
            _pendingPinStateStarted = 0;
        }

        if (_pendingPinState > -1 && millis() - _pendingPinStateStarted > Config::DEBOUNCE_MS)
        {
            auto stateChange = (_pendingPinState == _pressedPinState)
                                   ? BTN_CHANGE::Pressed
                                   : BTN_CHANGE::Released;

            dbg.dumpChange("lastPinState", _lastPinState, _pendingPinState);
            _lastPinState = _pendingPinState;
            _pendingPinState = -1;
            _pendingPinStateStarted = 0;
            dumpState(std::string("apply pending state. ") + std::to_string(static_cast<uint8_t>(stateChange)));

            if (stateChange == BTN_CHANGE::Released && _longPressFired)
            {
                _longPressFired = false;
                dumpState("Released skipped");
            }
            else
            {
                processEvent(stateChange);
            }

            if (stateChange == BTN_CHANGE::Pressed && _onLongPressed)
            {
                _longPressStartedAt = millis();
                dumpState(std::string("long press detection started ") + std::to_string(_longPressStartedAt));
            }
            else
            {
                _longPressStartedAt = 0;
            }
        }

        if (_longPressStartedAt > 0 && millis() - _longPressStartedAt > Config::LONG_PRESS_MS)
        {
            raiseLongPress();
            _longPressFired = true;
            _longPressStartedAt = 0;
            dumpState("LongPress raised");
        }
    }
};
