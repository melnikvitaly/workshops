#pragma once
#include <hardware/Debug.h>
#include <Config.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/timer_group_struct.h>
#include <esp_intr_alloc.h>
#include <esp_attr.h>

constexpr gpio_num_t FAN_PIN = GPIO_NUM_3;
constexpr gpio_num_t TACH_PIN = GPIO_NUM_2;

// Subtask 4: direct register access — no gptimer driver. All timer configuration,
// interrupt routing, and re-arming done by writing TIMERG0 registers directly.
// APB = 80 MHz, divider = 80 → 1 tick = 1 µs. autoreload resets counter to 0 on
// every alarm so each alarm_count is a plain duration, never an absolute timestamp.
class Task
{
public:
    Debug dbg{"task4"};

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
        uint32_t notifValue = 0;
        if (xTaskNotifyWait(0, 0xFFFFFFFF, &notifValue, pdMS_TO_TICKS(Config::TACH_REPORT_MS)))
            setFan(notifValue == 1);

        reportRpm();
    }

private:
    intr_handle_t _intrHandle = nullptr;
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
        ESP_ERROR_CHECK(gpio_set_level(FAN_PIN, 1));
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
            .intr_type = GPIO_INTR_NEGEDGE,
        };
        ESP_ERROR_CHECK(gpio_config(&conf));
        ESP_ERROR_CHECK(gpio_install_isr_service(0));
        ESP_ERROR_CHECK(gpio_isr_handler_add(TACH_PIN, tachIsr, this));
    }

    void setupTimer()
    {
        TIMERG0.hw_timer[0].config.en = 0; // stop while configuring

        TIMERG0.hw_timer[0].config.divider = Config::TIMER_CLK_DIVIDER;
        TIMERG0.hw_timer[0].config.increase = 1;   // count up
        TIMERG0.hw_timer[0].config.autoreload = 1; // reload from load regs on alarm
        TIMERG0.hw_timer[0].config.alarm_en = 0;

        // Load registers = 0: every alarm auto-resets counter back to 0
        TIMERG0.hw_timer[0].loadhi.val = 0;
        TIMERG0.hw_timer[0].loadlo = 0;
        TIMERG0.hw_timer[0].load.val = 1; // apply load now → counter = 0

        // First alarm: wait FAN_PERIOD_US then turn fan ON
        TIMERG0.hw_timer[0].alarmhi.val = (uint32_t)(Config::FAN_PERIOD_US >> 32);
        TIMERG0.hw_timer[0].alarmlo = (uint32_t)(Config::FAN_PERIOD_US & 0xFFFFFFFF);
        TIMERG0.hw_timer[0].config.alarm_en = 1;

        // Enable group-level interrupt for timer 0, then wire to a CPU interrupt
        TIMERG0.int_ena_timers.val |= BIT(0);
        ESP_ERROR_CHECK(esp_intr_alloc(ETS_TG0_T0_LEVEL_INTR_SOURCE,
                                       ESP_INTR_FLAG_IRAM,
                                       timerIsr, this, &_intrHandle));

        TIMERG0.hw_timer[0].config.en = 1; // start
    }

    void setFan(bool on)
    {
        _fanOn = on;
        if (!on)
            _pulseCount = 0;
        ESP_ERROR_CHECK(gpio_set_level(FAN_PIN, on ? 0 : 1));
        dbg.print(on ? "Fan ON" : "Fan OFF");
    }

    static void IRAM_ATTR tachIsr(void *arg)
    {
        Task *self = static_cast<Task *>(arg);
        if (self->_fanOn)
            self->_pulseCount = self->_pulseCount + 1;
    }

    static void IRAM_ATTR timerIsr(void *arg)
    {
        Task *self = static_cast<Task *>(arg);
        BaseType_t woken = pdFALSE;

        // Clear interrupt flag before re-arming — setting alarm_en first would
        // immediately re-fire the interrupt while the flag is still pending
        TIMERG0.int_clr_timers.val = BIT(0);

        bool nextOn = !self->_fanOn;
        uint64_t nextAlarm = nextOn ? Config::FAN_ON_US : Config::FAN_OFF_US;

        // Counter was auto-reloaded to 0 by hardware on alarm.
        // Set the next duration and re-arm — no manual counter reset needed.
        TIMERG0.hw_timer[0].alarmhi.val = (uint32_t)(nextAlarm >> 32);
        TIMERG0.hw_timer[0].alarmlo = (uint32_t)(nextAlarm & 0xFFFFFFFF);
        TIMERG0.hw_timer[0].config.alarm_en = 1;

        xTaskNotifyFromISR(self->_loopTask, nextOn ? 1u : 0u, eSetValueWithOverwrite, &woken);
        if (woken == pdTRUE)
            portYIELD_FROM_ISR();
    }
};
