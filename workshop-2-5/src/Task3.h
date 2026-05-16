#pragma once
#include <hardware/Debug.h>
#include <Config.h>
#include <driver/gpio.h>
#include <driver/gptimer.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

constexpr gpio_num_t FAN_PIN = GPIO_NUM_3;
constexpr gpio_num_t TACH_PIN = GPIO_NUM_2;

// Subtask 3: hardware gptimer — alarm callback runs in ISR context, cannot call GPIO
// or logging directly. One timer toggles between two intervals (ON / OFF duration).
// Fan state change is signalled to loop() via task notification.
class Task
{
public:
    Debug dbg{"task3"};

    void setup()
    {
        _loopTask = xTaskGetCurrentTaskHandle();
        setupRelay();
        setupTach();
        setupTimer();
        _lastRpmReport = (uint32_t)(esp_timer_get_time() / Config::US_PER_MS);
        dbg.print("Setup done, fan OFF, period=" + std::to_string(Config::FAN_PERIOD_US / Config::US_PER_MS) + "ms");
    }

    void loop()
    {
        // Block until alarm fires or TACH_REPORT_MS elapses — whichever comes first
        uint32_t notifValue = 0;
        if (xTaskNotifyWait(0, 0xFFFFFFFF, &notifValue, pdMS_TO_TICKS(Config::TACH_REPORT_MS)))
            setFan(notifValue == 1);

        reportRpm();
    }

private:
    gptimer_handle_t _timer = nullptr;
    TaskHandle_t _loopTask = nullptr;
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
        setFan(false);
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

    void setupTimer()
    {
        gptimer_config_t timer_conf = {};
        timer_conf.clk_src = GPTIMER_CLK_SRC_DEFAULT;
        timer_conf.direction = GPTIMER_COUNT_UP;
        timer_conf.resolution_hz = Config::TIMER_RESOLUTION_HZ;
        ESP_ERROR_CHECK(gptimer_new_timer(&timer_conf, &_timer));

        gptimer_event_callbacks_t cbs = {};
        cbs.on_alarm = onAlarm;
        ESP_ERROR_CHECK(gptimer_register_event_callbacks(_timer, &cbs, this));

        ESP_ERROR_CHECK(gptimer_enable(_timer));

        // First alarm: wait FAN_PERIOD_US, then turn fan ON
        gptimer_alarm_config_t alarm_conf = {};
        alarm_conf.alarm_count = Config::FAN_PERIOD_US;
        alarm_conf.reload_count = 0;
        alarm_conf.flags.auto_reload_on_alarm = true;
        ESP_ERROR_CHECK(gptimer_set_alarm_action(_timer, &alarm_conf));

        ESP_ERROR_CHECK(gptimer_start(_timer));
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

    // ISR context — reconfigure alarm for the next interval, then notify loop()
    static bool IRAM_ATTR onAlarm(gptimer_handle_t timer,
                                  const gptimer_alarm_event_data_t *edata,
                                  void *arg)
    {
        Task *self = static_cast<Task *>(arg);
        BaseType_t woken = pdFALSE;

        bool nextOn = !self->_fanOn;

        // Alternate between ON duration and the remaining OFF duration
        gptimer_alarm_config_t alarm = {
            .alarm_count = nextOn ? Config::FAN_ON_US : Config::FAN_OFF_US,
            .reload_count = 0,
            .flags = {.auto_reload_on_alarm = true},
        };
        gptimer_set_alarm_action(timer, &alarm);

        xTaskNotifyFromISR(self->_loopTask, nextOn ? 1u : 0u, eSetValueWithOverwrite, &woken);
        return woken == pdTRUE;
    }
};
