#pragma once
#include <hardware/Debug.h>
#include <driver/gpio.h>
#include <string>

constexpr gpio_num_t BTN_PIN = GPIO_NUM_5;

class Task
{
public:
    Debug dbg{"main"};

    void setup()
    {
        gpio_config_t io_conf = {
            .pin_bit_mask = (1ULL << BTN_PIN),
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_NEGEDGE,
        };
        gpio_config(&io_conf);
        gpio_install_isr_service(0);
        gpio_isr_handler_add(BTN_PIN, isr, this);
        dbg.print("Started");
    }

    void loop()
    {
        uint32_t cnt = _count;
        if (cnt != _lastReadCount)
        {
            _lastReadCount = cnt;
            int pin = gpio_get_level(BTN_PIN);
            dbg.print("count=" + std::to_string(cnt) + " pin=" + std::to_string(pin));
        }
    }

private:
    volatile uint32_t _count = 0;
    uint32_t _lastReadCount = 0;

    static void IRAM_ATTR isr(void *arg)
    {
        Task *self = static_cast<Task *>(arg);
        self->_count = self->_count + 1;
    }
};
