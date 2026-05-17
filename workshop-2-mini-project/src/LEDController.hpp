#pragma once
#include <driver/gptimer.h>
#include <esp_attr.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Owns the LED state machine (Manual ↔ Auto) and all timer/ISR coordination.
// All state mutations happen exclusively inside the FreeRTOS controller task; ISRs and
// timer callbacks only post notification bits – they never touch state directly.
// TLed must provide: begin(), setDuty(uint8_t), getDuty() const.
template<typename TLed>
class LEDController {
public:
    // Notification bits posted by button ISR and timers to the controller task.
    // Using bit flags with eSetBits lets multiple events arrive between xTaskNotifyWait calls without any being silently dropped.
    static constexpr uint32_t NOTIF_SHORT_PRESS  = (1u << 0);
    static constexpr uint32_t NOTIF_LONG_PRESS   = (1u << 1);
    static constexpr uint32_t NOTIF_AUTO_ADVANCE = (1u << 2);
    static constexpr uint32_t NOTIF_IDLE_TIMEOUT = (1u << 3);

private:
    enum class Mode { MANUAL, AUTO };

    TLed              &led_;
    Mode              mode_      = Mode::AUTO;
    uint8_t           stepIdx_   = 0;       // current index into Config::DUTY_STEPS
    TaskHandle_t      ctrlTask_  = nullptr;
    gptimer_handle_t  autoTimer_ = nullptr; // GP timer for Auto_MODE
    esp_timer_handle_t idleTimer_ = nullptr; // one-shot: fires after 10 s idle in MANUAL

    void advanceLedStep();
    void enterAuto();
    void enterManual();
    void startIdleTimer_();
    void handleBits(uint32_t bits);

    // GP timer alarm callback – runs in interrupt (ISR) context.
    // Only posts a notification bit; no state is touched here.
    // Returns true to request a context switch if the notified task is higher priority
    // than whatever was running, ensuring the step update happens without extra latency.
    static bool IRAM_ATTR autoTimerCb_(gptimer_handle_t timer,
                                       const gptimer_alarm_event_data_t *edata,
                                       void *ctx);


    // esp_timer callback – runs in the esp_timer service task (not ISR).
    // Uses xTaskNotify (non-ISR variant) because we are in a normal task context.
    static void idleTimerCb_(void *ctx);

    // FreeRTOS task entry point. Blocks indefinitely on task notifications.
    static void ctrlTaskEntry_(void *param);

    void initAutoTimer_();
    void initIdleTimer_();

public:
    explicit LEDController(TLed &led);

    void begin();

    void IRAM_ATTR isrShortPress(BaseType_t *woken);
    void IRAM_ATTR isrLongPress(BaseType_t *woken);
};
