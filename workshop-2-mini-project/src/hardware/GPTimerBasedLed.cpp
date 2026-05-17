#include "GPTimerBasedLed.hpp"
#include <esp_check.h>
#include <esp_log.h>
#include <Config.hpp>

#define LOG(fmt, ...) ESP_LOGI("GPTimerBasedLed", fmt, ##__VA_ARGS__)

static constexpr int LED_ON  = 0; // active-LOW wiring
static constexpr int LED_OFF = 1;

GPTimerBasedLed::GPTimerBasedLed(gpio_num_t pin) : pin_(pin) {}

bool IRAM_ATTR GPTimerBasedLed::timerCb_(gptimer_handle_t /*timer*/,
                                        const gptimer_alarm_event_data_t * /*edata*/,
                                        void *ctx)
{
    auto *self = static_cast<GPTimerBasedLed *>(ctx);
    PwmState &s = self->pwm_;

    gptimer_alarm_config_t alarm{};
    alarm.reload_count = 0;
    // counter resets to 0 on alarm so each phase is measured from zero, not absolute
    alarm.flags.auto_reload_on_alarm = 1;

    if (s.ledOn) {
        // End of ON phase → drive HIGH to turn LED off (active LOW)
        gpio_set_level(self->pin_, LED_OFF);
        alarm.alarm_count = s.offTicks;
        s.ledOn = false;
    } else {
        // End of OFF phase → drive LOW to turn LED on (active LOW)
        gpio_set_level(self->pin_, LED_ON);
        alarm.alarm_count = s.onTicks;
        s.ledOn = true;
    }
    
    gptimer_set_alarm_action(self->pwmTimer_, &alarm);
    
    return false;
}

void GPTimerBasedLed::begin()
{
    // Configure LED pin
    gpio_config_t io{};
    io.pin_bit_mask = 1ULL << pin_;
    io.mode         = GPIO_MODE_OUTPUT;
    io.pull_up_en   = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(pin_, 1); // LED off at startup

    // Create GP timer at 1 MHz (1 µs / tick)
    gptimer_config_t tcfg{};
    tcfg.clk_src       = GPTIMER_CLK_SRC_DEFAULT;
    tcfg.direction     = GPTIMER_COUNT_UP;
    tcfg.resolution_hz = Config::TIMER_RESOLUTION_HZ;
    // intr_priority 0 lets ESP-IDF select the allocation. For pure GPIO toggling the
    // default priority is sufficient; raising it would reduce jitter at the cost of
    // longer critical sections for other ISRs.
    tcfg.intr_priority = 0;
    ESP_ERROR_CHECK(gptimer_new_timer(&tcfg, &pwmTimer_));

    gptimer_event_callbacks_t cbs{};
    cbs.on_alarm = timerCb_;
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(pwmTimer_, &cbs, this));

    // gptimer_enable must be called exactly once after creation and before any
    // start/stop cycles. It allocates the interrupt resource.
    ESP_ERROR_CHECK(gptimer_enable(pwmTimer_));

    setDuty(0); // start with LED off
    LOG("init ok, pin=%d", pin_);
}

void GPTimerBasedLed::setDuty(uint8_t duty)
{
    gptimer_stop(pwmTimer_);

    duty_ = duty;

    if (duty == 0) {
        gpio_set_level(pin_, 1); // LED fully off
        return;
    }
    if (duty == 100) {
        gpio_set_level(pin_, 0); // LED fully on
        return;
    }
    
    constexpr uint64_t periodTicks =
        (uint64_t)Config::TIMER_RESOLUTION_HZ / Config::PWM_FREQ_HZ;

    pwm_.onTicks  = periodTicks * duty / 100;
    pwm_.offTicks = periodTicks - pwm_.onTicks;

    // Start in ON phase so LED brightness is visible immediately after a duty change
    gpio_set_level(pin_, 0); // GPIO LOW = LED on
    pwm_.ledOn = true;

    gptimer_alarm_config_t alarm{};
    alarm.alarm_count              = pwm_.onTicks;
    alarm.reload_count             = 0;
    alarm.flags.auto_reload_on_alarm = 1;
    ESP_ERROR_CHECK(gptimer_set_alarm_action(pwmTimer_, &alarm));
  
    ESP_ERROR_CHECK(gptimer_set_raw_count(pwmTimer_, 0));
    ESP_ERROR_CHECK(gptimer_start(pwmTimer_));

    LOG("duty=%u%% on=%llu off=%llu ticks", duty,
             (unsigned long long)pwm_.onTicks, (unsigned long long)pwm_.offTicks);
}
