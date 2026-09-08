#pragma once
#include <atomic>
#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "Ipc.hpp"
#include "Pinout.hpp"
#include "StateMachine.hpp"

// The safety task and the E-stop path (docs/architecture.md §2, §7).
//
// E-stop reaches this task two ways, and both just notify it:
//   - BTN_ESTOP GPIO4, a negedge ISR (the only button on an interrupt)
//   - a {"t":"estop"} line on any transport, forwarded by link_uart
//
// The task latches `estopLatched`. ctrl observes that atomic, drives the FSM to
// FAULT and emits the evt. Nothing here writes the UART or the FSM.

// The laser interlock (docs/interfaces.md §7). Pure atomic reads, so ctrl and ui
// can call it directly. The WDT-healthy term is added in task #6.
//
//   ZONE_TOUR - beam lit for the boot geometry check; there is no link yet
//   ARMED     - beam lit only while the selected channel is fresh
//   anything else, or E-stop latched - forced off
inline bool laserPermitted(const Ipc &ipc)
{
    if (ipc.estopLatched.load(std::memory_order_relaxed))
        return false;

    switch (ipc.state.load(std::memory_order_relaxed))
    {
    case State::ZoneTour:
        return true;
    case State::Armed:
        return ipc.linkFresh.load(std::memory_order_relaxed);
    default:
        return false;
    }
}

inline void IRAM_ATTR estopIsr(void *arg)
{
    Ipc *ipc = static_cast<Ipc *>(arg);
    ipc->estopSource.store(EstopSource::Button, std::memory_order_relaxed);
    BaseType_t higherWoken = pdFALSE;
    vTaskNotifyGiveFromISR(ipc->safetyTask, &higherWoken);
    portYIELD_FROM_ISR(higherWoken);
}

// Configure BTN_ESTOP and wire the ISR. Called from app_main AFTER
// ipc.safetyTask is populated, so the ISR can never fire into a null handle.
inline void installEstopIsr(Ipc &ipc)
{
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << pinout::BTN_ESTOP;
    io.mode         = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_ENABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_NEGEDGE;
    ESP_ERROR_CHECK(gpio_config(&io));

    const esp_err_t svc = gpio_install_isr_service(0);
    if (svc != ESP_OK && svc != ESP_ERR_INVALID_STATE) // already installed is fine
        ESP_ERROR_CHECK(svc);

    ESP_ERROR_CHECK(gpio_isr_handler_add(pinout::BTN_ESTOP, estopIsr, &ipc));
}

class SafetyTask
{
public:
    explicit SafetyTask(Ipc &ipc) : _ipc(ipc) {}

    static void entry(void *arg) { static_cast<SafetyTask *>(arg)->run(); }

    void run()
    {
        ESP_LOGI(TAG, "safety task up - waiting on E-stop");
        for (;;)
        {
            // Blocks forever until the ISR or link_uart gives the notification.
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
            _ipc.estopLatched.store(true, std::memory_order_relaxed);
            ESP_LOGW(TAG, "E-STOP latched (src=%d)",
                     (int)_ipc.estopSource.load(std::memory_order_relaxed));
        }
    }

private:
    static constexpr char TAG[] = "SAFETY";
    Ipc &_ipc;
};
