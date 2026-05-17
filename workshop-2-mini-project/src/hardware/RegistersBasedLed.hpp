#pragma once
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_intr_alloc.h>

// Generates a 10 kHz software PWM by toggling a GPIO inside a Timer Group 1 T0 ISR.
// Uses direct register access (timg_dev_t struct) instead of the gptimer driver API.
// Timer Group 1 T0 is reserved for this class; Timer Group 0 T0 is left free for
// LEDController::autoTimer_ which is allocated via the gptimer driver API.
// Active-LOW LED: GPIO LOW = LED on, GPIO HIGH = LED off.
class RegistersBasedLed {
private:
    gpio_num_t    pin_;
    intr_handle_t isrHandle_ = nullptr;
    uint8_t       duty_      = 0;

    // Precomputed TIMG1 T0 config register value (APB clock, count-up, autoreload,
    // prescaler). alarm_en (bit 10) and en (bit 31) are OR-ed in when the timer runs.
    // Written once in begin() and read from the ISR, so must stay in DRAM (default).
    uint32_t baseCfg_ = 0;

    // Shared state between setDuty() (task context) and the timer ISR.
    // Race-free on single-core C3 because setDuty() stops the counter before writing
    // and restarts it after, so the ISR cannot execute concurrently with the update.
    // On dual-core S3 this matches the same contract as GPTimerBasedLed.
    struct PwmState {
        uint64_t onTicks;   // ticks GPIO stays LOW  (LED on)
        uint64_t offTicks;  // ticks GPIO stays HIGH (LED off)
        bool     ledOn;     // true while LED is on (GPIO LOW, active-LOW)
    } pwm_{};

    // IRAM_ATTR: must execute even when flash cache is disabled.
    static void IRAM_ATTR timerIsr_(void *ctx);

    // Shim needed on C3 when USE_REGISTER_INTR is set: CLIC vector slots carry
    // no argument, so this free-function wrapper calls timerIsr_ with s_instance.
    friend void timerIsrShim_();

    void initGpio_();   // configure IO_MUX and initial GPIO state
    void initClock_();  // enable TIMERG1 bus clock and reset module registers
    void initTimer_();  // configure TIMG1 T0 prescaler, reload, and interrupt enable

public:
    explicit RegistersBasedLed(gpio_num_t pin);

    void    begin();
    void    setDuty(uint8_t duty);
    uint8_t getDuty() const { return duty_; }
};
