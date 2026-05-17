#include "Button.hpp"
#include <esp_log.h>
#include <esp_timer.h>
#include <Config.hpp>

#define LOG(fmt, ...) ESP_LOGI("Button", fmt, ##__VA_ARGS__)

void IRAM_ATTR Button::isr_(void *arg)
{
    auto *self = static_cast<Button *>(arg);

    int level = gpio_get_level(self->pin_);

    if (level == 0)
    {
        self->pressStartUs_ = esp_timer_get_time();
        return;
    }

    int64_t durUs = esp_timer_get_time() - self->pressStartUs_;

    if (durUs < (int64_t)Config::MIN_PRESS_US)
        return;

    BaseType_t woken = pdFALSE;
    if (durUs >= (int64_t)Config::LONG_PRESS_US) {
        if (self->longCb_)  self->longCb_ (self->longCtx_,  &woken);
    } else {
        if (self->shortCb_) self->shortCb_(self->shortCtx_, &woken);
    }
    portYIELD_FROM_ISR(woken);
}

void Button::begin(gpio_num_t pin)
{
    pin_ = pin;

    gpio_config_t gcfg{};
    gcfg.pin_bit_mask = 1ULL << pin_;
    gcfg.mode = GPIO_MODE_INPUT;
    gcfg.pull_up_en = GPIO_PULLUP_ENABLE;
    gcfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gcfg.intr_type = GPIO_INTR_ANYEDGE;
    ESP_ERROR_CHECK(gpio_config(&gcfg));

    esp_err_t ret = gpio_install_isr_service(0);
    if (ret != ESP_ERR_INVALID_STATE)
    {
        ESP_ERROR_CHECK(ret);
    }

    ESP_ERROR_CHECK(gpio_isr_handler_add(pin_, isr_, this));

    LOG("init ok pin=%d", (int)pin_);
}
