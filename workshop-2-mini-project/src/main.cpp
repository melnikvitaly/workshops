#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <hardware/Button.hpp>
#include <hardware/GPTimerBasedLed.hpp>
#include <hardware/RegistersBasedLed.hpp>
#include "LEDController.hpp"

using LedDriver = RegistersBasedLed; // switch here: GPTimerBasedLed | RegistersBasedLed


#ifndef PIN_LED
#  error "PIN_LED not defined — set PIN_LED in the board build_flags"
#endif
#ifndef PIN_BUTTON
#  error "PIN_BUTTON not defined — set PIN_BUTTON in the board build_flags"
#endif

#pragma region PIN_DEFINITIONS
static constexpr gpio_num_t LED_GPIO    = (gpio_num_t)PIN_LED;
static constexpr gpio_num_t BUTTON_GPIO = (gpio_num_t)PIN_BUTTON;
#pragma endregion

static LedDriver               led(LED_GPIO);
static LEDController<LedDriver> controller(led);
static Button                  button;


extern "C" void app_main()
{
    controller.begin();// create own task to function

    button.onShortPress<&LEDController<LedDriver>::isrShortPress>(&controller);
    button.onLongPress <&LEDController<LedDriver>::isrLongPress >(&controller);
    button.begin(BUTTON_GPIO);

    ESP_LOGI("main", "boot complete");
    vTaskDelete(nullptr);
}
