#include "LEDController.hpp"
#include <esp_check.h>
#include <esp_log.h>
#include "Config.hpp"
#include <hardware/GPTimerBasedLed.hpp>
#include <hardware/RegistersBasedLed.hpp>

#define LOG(fmt, ...) ESP_LOGI("LEDController", fmt, ##__VA_ARGS__)

template<typename TLed>
LEDController<TLed>::LEDController(TLed &led) : led_(led) {}

template<typename TLed>
void LEDController<TLed>::advanceLedStep()
{
    stepIdx_ = (stepIdx_ + 1) % Config::NUM_STEPS;
    uint8_t duty = Config::DUTY_STEPS[stepIdx_];
    led_.setDuty(duty);
    LOG("step→%u duty=%u%%", stepIdx_, duty);
}

template<typename TLed>
void LEDController<TLed>::enterAuto()
{
    switch (mode_)
    {
        case Mode::AUTO:
            return;
        case Mode::MANUAL:
            mode_ = Mode::AUTO;
            esp_timer_stop(idleTimer_); // stop idle countdown – no longer in manual mode
            gptimer_set_raw_count(autoTimer_, 0);
            gptimer_start(autoTimer_);
            LOG("→ AUTO");
            break;
    }
}

template<typename TLed>
void LEDController<TLed>::enterManual()
{
    switch (mode_)
    {
        case Mode::MANUAL:
            return;
        case Mode::AUTO:
            mode_ = Mode::MANUAL;
            gptimer_stop(autoTimer_);
            startIdleTimer_(); // begin countdown toward auto re-entry
            LOG("→ MANUAL");
            break;
    }
}

template<typename TLed>
void LEDController<TLed>::startIdleTimer_()
{
    // esp_timer_stop returns ESP_ERR_INVALID_STATE if the timer is not running
    esp_timer_stop(idleTimer_);
    ESP_ERROR_CHECK(esp_timer_start_once(idleTimer_, Config::IDLE_TIMEOUT_US));
}

// ── Notification handler (runs only inside ctrlTask_) ────────────────────────

template<typename TLed>
void LEDController<TLed>::handleBits(uint32_t bits)
{
    if (bits & NOTIF_SHORT_PRESS)
    {
        switch (mode_)
        {
            case Mode::AUTO:
                enterManual();
                break;
            case Mode::MANUAL:
                advanceLedStep();
                startIdleTimer_(); // reset idle countdown on every manual interaction
                break;
        }
        // Clear the idle timeout bit when a short press arrived in the same notification
        // word. Without this, both events would be processed in the same call and the
        // idle timeout would immediately override the short press.
        bits &= ~NOTIF_IDLE_TIMEOUT;
    }

    if (bits & NOTIF_LONG_PRESS)
    {
        enterAuto();
    }

    if (bits & NOTIF_AUTO_ADVANCE)
    {
        switch (mode_)
        {
            case Mode::AUTO:   advanceLedStep(); break;
            case Mode::MANUAL: break;
        }
    }

    if (bits & NOTIF_IDLE_TIMEOUT)
    {
        switch (mode_)
        {
            case Mode::MANUAL: enterAuto(); break;
            case Mode::AUTO:   break;
        }
    }
}

template<typename TLed>
bool LEDController<TLed>::autoTimerCb_(gptimer_handle_t /*timer*/,
                                                  const gptimer_alarm_event_data_t * /*edata*/,
                                                  void *ctx)
{
    auto *self = static_cast<LEDController<TLed> *>(ctx);
    BaseType_t woken = pdFALSE;
    xTaskNotifyFromISR(self->ctrlTask_, NOTIF_AUTO_ADVANCE, eSetBits, &woken);
    return woken == pdTRUE;
}

template<typename TLed>
void LEDController<TLed>::isrShortPress(BaseType_t *woken)
{
    xTaskNotifyFromISR(ctrlTask_, NOTIF_SHORT_PRESS, eSetBits, woken);
}

template<typename TLed>
void LEDController<TLed>::isrLongPress(BaseType_t *woken)
{
    xTaskNotifyFromISR(ctrlTask_, NOTIF_LONG_PRESS, eSetBits, woken);
}

template<typename TLed>
void LEDController<TLed>::idleTimerCb_(void *ctx)
{
    auto *self = static_cast<LEDController<TLed> *>(ctx);
    xTaskNotify(self->ctrlTask_, NOTIF_IDLE_TIMEOUT, eSetBits);
}

template<typename TLed>
void LEDController<TLed>::ctrlTaskEntry_(void *thisPtr)
{
    auto *self = static_cast<LEDController<TLed> *>(thisPtr);
    for (;;)
    {
        uint32_t bits = 0;
        // pdFALSE for clearOnEntry keeps any bits that arrived before this wait.
        // ULONG_MAX for clearOnExit atomically clears all bits after reading, so no
        // event is double-processed across iterations.
        xTaskNotifyWait(pdFALSE, ULONG_MAX, &bits, portMAX_DELAY);
        self->handleBits(bits);
    }
}

template<typename TLed>
void LEDController<TLed>::initAutoTimer_()
{
    gptimer_config_t tcfg{};
    tcfg.clk_src = GPTIMER_CLK_SRC_DEFAULT;
    tcfg.direction = GPTIMER_COUNT_UP;
    tcfg.resolution_hz = Config::TIMER_RESOLUTION_HZ; // 1 µs / tick
    tcfg.intr_priority = 0;
    ESP_ERROR_CHECK(gptimer_new_timer(&tcfg, &autoTimer_));

    gptimer_event_callbacks_t cbs{};
    cbs.on_alarm = autoTimerCb_;
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(autoTimer_, &cbs, this));
    
    gptimer_alarm_config_t alarm{};
    alarm.alarm_count = Config::AUTO_ADVANCE_US;
    alarm.reload_count = 0;
    alarm.flags.auto_reload_on_alarm = 1;
    ESP_ERROR_CHECK(gptimer_set_alarm_action(autoTimer_, &alarm));
    
    ESP_ERROR_CHECK(gptimer_enable(autoTimer_));
}

template<typename TLed>
void LEDController<TLed>::initIdleTimer_()
{
    esp_timer_create_args_t args{};
    args.callback = idleTimerCb_;
    args.arg = this;    
    args.dispatch_method = ESP_TIMER_TASK;
    args.name = "idle_tmr";    
    ESP_ERROR_CHECK(esp_timer_create(&args, &idleTimer_));
}

template<typename TLed>
void LEDController<TLed>::begin()
{
    led_.begin();
    
    xTaskCreate(ctrlTaskEntry_, "led_ctrl", Config::CTRL_TASK_STACK, this, Config::CTRL_TASK_PRIORITY, &ctrlTask_);

    initAutoTimer_();
    initIdleTimer_();

    switch (mode_)
    {
        case Mode::MANUAL:
            ESP_ERROR_CHECK(esp_timer_start_once(idleTimer_, Config::IDLE_TIMEOUT_US));
            break;
        case Mode::AUTO:
            break;
    }

    LOG("begin ok, mode=%s duty=%u%%",
        mode_ == Mode::AUTO ? "AUTO" : "MANUAL",
        Config::DUTY_STEPS[stepIdx_]);
}

// Explicit instantiations — keeps all implementation in this TU.
template class LEDController<GPTimerBasedLed>;
template class LEDController<RegistersBasedLed>;
