#pragma once
#include <driver/gpio.h>
#include <driver/gptimer.h>
#include <esp_attr.h>

// Generates a 10 kHz software PWM by toggling a GPIO inside a GP timer alarm ISR.
// LEDC hardware peripheral is intentionally NOT used (spec requirement).
// Active-LOW LED: GPIO LOW = LED on, GPIO HIGH = LED off.
class GPTimerBasedLed {
private:
    gpio_num_t       pin_;
    gptimer_handle_t pwmTimer_ = nullptr;
    uint8_t          duty_     = 0;

    // Shared state between the task (setDuty) and the GP timer ISR.
    // Race-free because setDuty always calls gptimer_stop before writing and
    // gptimer_start after, so the ISR cannot be executing concurrently with the update.
    struct PwmState {
        uint64_t onTicks;   // ticks the GPIO stays LOW  (LED on)
        uint64_t offTicks;  // ticks the GPIO stays HIGH (LED off)
        bool     ledOn; // true while LED is on (GPIO LOW, active-LOW)
    } pwm_{};

    // Must be IRAM_ATTR because this callback runs in interrupt context.
    // If flash cache is disabled (e.g. during flash writes), IRAM functions still execute.
    // gpio_set_level and gptimer_set_alarm_action are both IRAM-safe per ESP-IDF docs.
    static bool IRAM_ATTR timerCb_(gptimer_handle_t timer,
                                   const gptimer_alarm_event_data_t *edata,
                                   void *ctx);

public:
    explicit GPTimerBasedLed(gpio_num_t pin);

    void    begin();
    void    setDuty(uint8_t duty);
    uint8_t getDuty() const { return duty_; }
};
