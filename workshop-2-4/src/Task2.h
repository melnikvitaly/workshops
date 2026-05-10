#pragma once
#include <hardware/Debug.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <Config.h>
#include <string>

class Task
{
    static constexpr gpio_num_t PIN = GPIO_NUM_5;
    static constexpr int64_t DEBOUNCE_US = Config::DEBOUNCE_MS * 1000LL;

public:
    Debug dbg{"task2"};

    void setup()
    {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << PIN),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_NEGEDGE,
        };
        gpio_config(&io_conf);
        gpio_install_isr_service(0);
        gpio_isr_handler_add(PIN, isr, this);
        dbg.print("Started");
    }

    void loop()
    {
        if (!_pendingIrq)
            return;

        int64_t irqTime;
        portENTER_CRITICAL(&_mux); //reading/writing of 64 bit is not atomic on 32-bit esp systems
        irqTime = _irqTimeUs;
        _pendingIrq = false;
        portEXIT_CRITICAL(&_mux);

        if ((irqTime - _lastAcceptedTimeUs) < DEBOUNCE_US)
            return; // ignore: bounce within debounce window

        _lastAcceptedTimeUs = irqTime;
        _count++;
        dbg.print("count=" + std::to_string(_count));
    }

private:
    portMUX_TYPE _mux = portMUX_INITIALIZER_UNLOCKED;
    volatile bool _pendingIrq = false;
    volatile int64_t _irqTimeUs = 0;
    int64_t _lastAcceptedTimeUs = 0;
    uint32_t _count = 0;

    static void IRAM_ATTR isr(void *arg)
    {
        Task *self = static_cast<Task *>(arg);
        portENTER_CRITICAL_ISR(&self->_mux);
        self->_irqTimeUs = esp_timer_get_time();
        self->_pendingIrq = true;
        portEXIT_CRITICAL_ISR(&self->_mux);
    }
};
