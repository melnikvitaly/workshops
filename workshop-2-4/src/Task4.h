#pragma once
#include <hardware/Debug.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <Config.h>
#include <string>

class Task
{
    static constexpr gpio_num_t PIN = GPIO_NUM_5;
    static constexpr int64_t POLL_INTERVAL_US = Config::POLL_INTERVAL_MS * 1000LL;
    static constexpr int64_t DEBOUNCE_US = Config::DEBOUNCE_MS * 1000LL;

    enum class State { RELEASED, PRESS_DEBOUNCE, PRESSED, RELEASE_DEBOUNCE };

public:
    Debug dbg{"task4"};

    void setup()
    {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << PIN),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        gpio_config(&io_conf);
        dbg.print("Started");
    }

    void loop()
    {
        int64_t now = esp_timer_get_time();
        if ((now - _lastPollUs) < POLL_INTERVAL_US)
            return;
        _lastPollUs = now;

        int pin = gpio_get_level(PIN);

        switch (_state)
        {
        case State::RELEASED:
            if (pin == 0)
            {
                _state = State::PRESS_DEBOUNCE;
                _stateEnteredUs = now;
            }
            break;

        case State::PRESS_DEBOUNCE:
            if (pin != 0)
                _state = State::RELEASED; // bounced — restart
            else if ((now - _stateEnteredUs) >= DEBOUNCE_US)
            {
                _state = State::PRESSED;
                _count++;
                dbg.print("count=" + std::to_string(_count));
            }
            break;

        case State::PRESSED:
            if (pin != 0)
            {
                _state = State::RELEASE_DEBOUNCE;
                _stateEnteredUs = now;
            }
            break;

        case State::RELEASE_DEBOUNCE:
            if (pin == 0)
                _state = State::PRESSED; // bounced — restart
            else if ((now - _stateEnteredUs) >= DEBOUNCE_US)
                _state = State::RELEASED;
            break;
        }
    }

private:
    State _state = State::RELEASED;
    int64_t _lastPollUs = 0;
    int64_t _stateEnteredUs = 0;
    uint32_t _count = 0;
};
