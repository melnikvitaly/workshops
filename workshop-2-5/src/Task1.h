#pragma once
#include <hardware/Debug.h>
#include <Config.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/timers.h>

constexpr gpio_num_t FAN_PIN  = GPIO_NUM_3;
constexpr gpio_num_t TACH_PIN = GPIO_NUM_2;

// Subtask 1: FreeRTOS software timers — callbacks run in the shared timer daemon task with
// a limited stack, so GPIO and logging are deferred to loop() via a flag. Timer identity
// is retrieved via pvTimerGetTimerID() instead of a raw void* arg.
// Optional enhancements implemented: watchdog, transition logging, re-entry guard, LED indicator.
class Task
{
public:
    Debug dbg{"task1"};

    void setup()
    {
        reportResetReason();
        setupRelay();
        setupTach();
        setupTimers();
        setupWatchdog();
        _lastRpmReport  = (uint32_t)(esp_timer_get_time() / Config::US_PER_MS);
        _lastTransition = _lastRpmReport;
        _startTime      = _lastRpmReport;
        dbg.print("Setup done, fan OFF, period=" + std::to_string(Config::FAN_PERIOD_US / Config::US_PER_MS) + "ms");
    }

    void loop()
    {
        ESP_ERROR_CHECK(esp_task_wdt_reset());

        // Simulate a hang after 20 s — watchdog will trigger a reset
        if ((uint32_t)(esp_timer_get_time() / Config::US_PER_MS) - _startTime >= Config::HANG_AFTER_MS)
        {
            dbg.print("Hanging...");
            while (true) {}
        }

        // FreeRTOS timer callbacks run in the timer daemon task — defer GPIO + logging here
        if (_pendingFanOn != _fanOn)
            setFan(_pendingFanOn);

        reportRpm();
    }

private:
    TimerHandle_t _periodTimer   = nullptr;
    TimerHandle_t _durationTimer = nullptr;

    volatile uint32_t _pulseCount   = 0;
    volatile bool     _fanOn        = false;
    volatile bool     _pendingFanOn = false;
    uint32_t          _lastRpmReport  = 0;
    uint32_t          _lastTransition = 0;
    uint32_t          _startTime      = 0;

    void reportResetReason()
    {
#define RST_CASE(x) case x: reason = #x; break;
        const char *reason;
        switch (esp_reset_reason())
        {
            RST_CASE(ESP_RST_UNKNOWN)
            RST_CASE(ESP_RST_POWERON)
            RST_CASE(ESP_RST_EXT)
            RST_CASE(ESP_RST_SW)
            RST_CASE(ESP_RST_PANIC)
            RST_CASE(ESP_RST_INT_WDT)
            RST_CASE(ESP_RST_TASK_WDT)
            RST_CASE(ESP_RST_WDT)
            RST_CASE(ESP_RST_DEEPSLEEP)
            RST_CASE(ESP_RST_BROWNOUT)
            RST_CASE(ESP_RST_SDIO)
            RST_CASE(ESP_RST_USB)
            RST_CASE(ESP_RST_JTAG)
            default: reason = "unhandled"; break;
        }
#undef RST_CASE
        dbg.print(std::string("Reset reason: ") + reason);
    }

    void setupWatchdog()
    {
        esp_task_wdt_config_t cfg = {
            .timeout_ms    = Config::WDT_TIMEOUT_MS,
            .idle_core_mask = 0,
            .trigger_panic  = true,  // reset device on timeout
        };
        esp_err_t err = esp_task_wdt_reconfigure(&cfg);
        if (err == ESP_ERR_INVALID_STATE)
            ESP_ERROR_CHECK(esp_task_wdt_init(&cfg));
        else
            ESP_ERROR_CHECK(err);
        ESP_ERROR_CHECK(esp_task_wdt_add(nullptr));
    }

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
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&conf));
        setFan(false);
    }

    void setupTach()
    {
        gpio_config_t conf = {
            .pin_bit_mask = (1ULL << TACH_PIN),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_NEGEDGE, // open-drain tach: pulse = LOW
        };
        ESP_ERROR_CHECK(gpio_config(&conf));
        ESP_ERROR_CHECK(gpio_install_isr_service(0));
        ESP_ERROR_CHECK(gpio_isr_handler_add(TACH_PIN, tachIsr, this));
    }

    void setupTimers()
    {
        _periodTimer = xTimerCreate(
            "fan_period",
            pdMS_TO_TICKS(Config::FAN_PERIOD_US / Config::US_PER_MS),
            pdTRUE, // auto-reload
            this,
            onPeriodTimer);
        _durationTimer = xTimerCreate(
            "fan_duration",
            pdMS_TO_TICKS(Config::FAN_ON_US / Config::US_PER_MS),
            pdFALSE, // one-shot
            this,
            onDurationTimer);
        assert(_periodTimer && _durationTimer);
        xTimerStart(_periodTimer, portMAX_DELAY);
    }

    void setFan(bool on)
    {
        uint32_t now     = (uint32_t)(esp_timer_get_time() / Config::US_PER_MS);
        uint32_t elapsed = now - _lastTransition;
        _lastTransition  = now;

        _fanOn = on;
        if (!on)
            _pulseCount = 0; // discard noise from relay switching
        ESP_ERROR_CHECK(gpio_set_level(FAN_PIN, on ? 0 : 1)); // active-low relay, LED mirrors
        dbg.print(std::string(on ? "Fan ON" : "Fan OFF") +
                  " elapsed=" + std::to_string(elapsed) + "ms");
    }

    static void IRAM_ATTR tachIsr(void *arg)
    {
        Task *self = static_cast<Task *>(arg);
        if (self->_fanOn)
            self->_pulseCount = self->_pulseCount + 1;
    }

    // Re-entry guard: skip if fan is already on or pending — period fired before OFF completed
    static void onPeriodTimer(TimerHandle_t timer)
    {
        Task *self = static_cast<Task *>(pvTimerGetTimerID(timer));
        if (self->_fanOn || self->_pendingFanOn)
            return;
        self->_pendingFanOn = true;
        xTimerStart(self->_durationTimer, 0);
    }

    static void onDurationTimer(TimerHandle_t timer)
    {
        Task *self = static_cast<Task *>(pvTimerGetTimerID(timer));
        self->_pendingFanOn = false;
    }
};
