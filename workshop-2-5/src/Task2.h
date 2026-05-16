#pragma once
#include <hardware/Debug.h>
#include <Config.h>
#include <driver/gpio.h>
#include <esp_timer.h>

constexpr gpio_num_t FAN_PIN = GPIO_NUM_3;
constexpr gpio_num_t TACH_PIN = GPIO_NUM_2;

// Subtask 2: esp_timer — callbacks run in a dedicated high-priority task, safe to call
// GPIO and logging directly. Resolution is microseconds, unlike FreeRTOS tick-based timers.
class Task
{
public:
    Debug dbg{"task2"};

    void setup()
    {
        setupRelay();
        setupTach();
        setupTimers();
        _lastRpmReport = (uint32_t)(esp_timer_get_time() / Config::US_PER_MS);
        dbg.print("Setup done, fan OFF, period=" + std::to_string(Config::FAN_PERIOD_US / Config::US_PER_MS) + "ms");
    }

    void loop()
    {
        reportRpm();
    }

private:
    esp_timer_handle_t _periodTimer = nullptr;
    esp_timer_handle_t _durationTimer = nullptr;

    volatile uint32_t _pulseCount = 0;
    volatile bool _fanOn = false;
    uint32_t _lastRpmReport = 0;

    void reportRpm()
    {
        uint32_t now = (uint32_t)(esp_timer_get_time() / Config::US_PER_MS);
        if (now - _lastRpmReport < Config::TACH_REPORT_MS)
            return;
        uint32_t pulses = _pulseCount;
        _pulseCount = 0;
        _lastRpmReport = now;
        uint32_t rpm = (pulses * Config::MS_PER_MINUTE) / (Config::TACH_PULSES_PER_REV * Config::TACH_REPORT_MS);
        dbg.print("RPM=" + std::to_string(rpm));
    }

    void setupRelay()
    {
        ESP_ERROR_CHECK(gpio_set_level(FAN_PIN, 1)); // hold relay off before gpio_config applies
        gpio_config_t conf = {
            .pin_bit_mask = (1ULL << FAN_PIN),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&conf));
        setFan(true);
    }

    void setupTach()
    {
        gpio_config_t conf = {
            .pin_bit_mask = (1ULL << TACH_PIN),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_NEGEDGE, // open-drain tach: pulse = LOW
        };
        ESP_ERROR_CHECK(gpio_config(&conf));
        ESP_ERROR_CHECK(gpio_install_isr_service(0));
        ESP_ERROR_CHECK(gpio_isr_handler_add(TACH_PIN, tachIsr, this));
    }

    void setupTimers()
    {
        esp_timer_create_args_t period_args = {
            .callback = onPeriodTimer,
            .arg = this,
            .name = "fan_period",
        };
        esp_timer_create_args_t duration_args = {
            .callback = onDurationTimer,
            .arg = this,
            .name = "fan_duration",
        };
        ESP_ERROR_CHECK(esp_timer_create(&period_args, &_periodTimer));
        ESP_ERROR_CHECK(esp_timer_create(&duration_args, &_durationTimer));
        ESP_ERROR_CHECK(esp_timer_start_periodic(_periodTimer, Config::FAN_PERIOD_US));
    }

    void setFan(bool on)
    {
        _fanOn = on;
        if (!on)
            _pulseCount = 0;                                  // discard noise from relay switching
        ESP_ERROR_CHECK(gpio_set_level(FAN_PIN, on ? 0 : 1)); // active-low relay
        dbg.print(on ? "Fan ON" : "Fan OFF");
    }

    static void IRAM_ATTR tachIsr(void *arg)
    {
        Task *self = static_cast<Task *>(arg);
        if (self->_fanOn)
            self->_pulseCount = self->_pulseCount + 1;
    }

    static void onPeriodTimer(void *arg)
    {
        Task *self = static_cast<Task *>(arg);
        self->setFan(true);
        ESP_ERROR_CHECK(esp_timer_start_once(self->_durationTimer, Config::FAN_ON_US));
    }

    static void onDurationTimer(void *arg)
    {
        Task *self = static_cast<Task *>(arg);
        self->setFan(false);
    }
};
