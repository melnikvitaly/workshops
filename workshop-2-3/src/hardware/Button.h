#pragma once
#include <Arduino.h>
#include <hardware/Debug.h>
#include <Config.h>

class Button
{
public:
    using Callback = std::function<void()>;

private:
    uint8_t _buttonPin;
    int _pressedPinState;
    int _lastPinState;
    volatile int _pendingPinState = -1;
    volatile unsigned long _pendingPinStateStarted;
    unsigned long _longPressStartedAt;
    bool _longPressFired;
    Debug &dbg;
    Callback _onPress;
    Callback _onRelease;
    Callback _onLongPressed;
    bool _useISR = false;
    volatile bool _isrPressed = false;

public:
    Button(uint8_t buttonPin, int pressedState, bool useISR, Debug &dbg) : _buttonPin(buttonPin),
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
        pinMode(_buttonPin, INPUT | (_pressedPinState == LOW ? PULLUP : PULLDOWN));
        _lastPinState = digitalRead(_buttonPin);
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
        _isrPressed = digitalRead(_buttonPin);
    }

    static void IRAM_ATTR changeIsr(void *arg)
    {
        Button *btn = (Button *)arg;
        btn->onIsrChange();
    }

    void attachInterrupts()
    {
        attachInterruptArg(digitalPinToInterrupt(_buttonPin), changeIsr, (void *)this, CHANGE);
    }

    void processEvent(BTN_CHANGE stateChange)
    {
        if (stateChange == BTN_CHANGE::Pressed)
        {
            if (_onPress)
            {
                _onPress();
            }
        }
        else if (stateChange == BTN_CHANGE::Released)
        {
            if (_onRelease)
            {
                _onRelease();
            }
        }
    }
    void dumpState(String msg)
    {
        dbg.print(msg + "," + _lastPinState + "," + _pendingPinState);
    }

    void raiseLongPress()
    {
        if (_onLongPressed)
        {
            _onLongPressed();
        }
    }

    int readNewValue()
    {
        if (_useISR)
        {
            return _isrPressed;
        }
        else
        {
            return digitalRead(_buttonPin);
        }
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
                                   ? Button::BTN_CHANGE::Pressed
                                   : Button::BTN_CHANGE::Released;

            dbg.dumpChange("lastPinState", _lastPinState, _pendingPinState);
            _lastPinState = _pendingPinState;

            _pendingPinState = -1;
            _pendingPinStateStarted = 0;
            dumpState(String("apply pending state. ") + static_cast<uint8_t>(stateChange));
            if (stateChange == Button::BTN_CHANGE::Released && _longPressFired)
            {
                _longPressFired = false;
                dumpState("Released skipped");
            }
            else
            {
                processEvent(stateChange);
            }

            if (stateChange == Button::BTN_CHANGE::Pressed && _onLongPressed)
            {
                _longPressStartedAt = millis();
                dumpState(String("long press detection started ") + _longPressStartedAt);
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
