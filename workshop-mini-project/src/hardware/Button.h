#pragma once
#include <Arduino.h>
#include "DebugOut.h"

constexpr uint8_t DEBOUNCE_MS = 20;
constexpr uint16_t LONG_PRESS_MS = 3000;
class Button
{
public:
    using Callback = std::function<void()>;

private:
    uint8_t _buttonPin;            // PIN for BUTTON
    int _pressedPinState;          // LOW or HIGH on pressed
    int _lastPinState;             // Last validated state of the pin
    int _pendingPinState = -1;     // PIN to apply after DEBOUNCE_MS
    ulong _pendingPinStateStarted; // Debounce start time
    ulong _longPressStartedAt;     // When button was pressed to detect LongPressed
    bool _longPressFired;          // flag to cancel Released after LongPress event
    DebugOut &dbg;
    Callback _onPress;
    Callback _onRelease;
    Callback _onLongPressed;

public:
    Button(uint8_t buttonPin, int pressedState, DebugOut &dbg) : _buttonPin(buttonPin),
                                                                 _pressedPinState(pressedState),
                                                                 _lastPinState(!pressedState),
                                                                 _longPressStartedAt(0),
                                                                 _longPressFired(false),
                                                                 dbg(dbg)
    {
    }
    enum class BTN_CHANGE
    {
        Pressed,
        Released,
        None
    };
    void init()
    {
        pinMode(_buttonPin, INPUT | (_pressedPinState == LOW ? PULLUP : PULLDOWN));
        _lastPinState = digitalRead(_buttonPin);
    }

    void onPress(Callback cb) { _onPress = cb; }
    void onRelease(Callback cb) { _onRelease = cb; }
    void onLongPress(Callback cb) { _onLongPressed = cb; }

    BTN_CHANGE wasStateChanged(uint8_t delayMs = DEBOUNCE_MS)
    {
        int val = digitalRead(_buttonPin);
        if (_lastPinState != val)
        {
            delay(delayMs);
            val = digitalRead(_buttonPin);
        }
        if (val != _lastPinState)
        {
            _lastPinState = val;
            return val == _pressedPinState ? BTN_CHANGE::Pressed : BTN_CHANGE::Released;
        }
        return BTN_CHANGE::None;
    }

    void updateSyncWithEvents()
    {
        auto stateChange = wasStateChanged();
        processEvent(stateChange);
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
    void dumpChange(uint8_t from, uint8_t to)
    {
        dbg.print(from + String("=>") + to);
    }

    void raiseLongPress()
    {
        if (_onLongPressed)
        {
            _onLongPressed();
        }
    }

    void tick()
    {
        // Read potential new state
        auto val = digitalRead(_buttonPin);
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

        // Check pending new state
        if (_pendingPinState > -1 && millis() - _pendingPinStateStarted > DEBOUNCE_MS)
        {

            auto stateChange = (_pendingPinState == _pressedPinState)
                                   ? Button::BTN_CHANGE::Pressed
                                   : Button::BTN_CHANGE::Released;

            dumpChange(_lastPinState, _pendingPinState);
            _lastPinState = _pendingPinState;

            _pendingPinState = -1;
            _pendingPinStateStarted = 0;
            dumpState("apply pending");
            if (stateChange == Button::BTN_CHANGE::Released && _longPressFired)
            {
                _longPressFired = false;
                // Do not raise Released event after longPressed
                dumpState("Released skipped");
            }
            else
            {
                processEvent(stateChange);
            }

            // Start long press detection
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

        // Check long press
        if (_longPressStartedAt > 0 && millis() - _longPressStartedAt > LONG_PRESS_MS)
        {
            raiseLongPress();
            _longPressFired = true;
            _longPressStartedAt = 0;
            dumpState("LongPress raised");
        }
    }
};