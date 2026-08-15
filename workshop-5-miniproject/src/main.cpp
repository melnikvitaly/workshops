#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "Config.hpp"
#include "Device.hpp"
#include "DeviceController.hpp"
#include "AppLogger.hpp"

static const char *TAG = "ESP";

// Holds every part and its driver, plus the single input source.
static Device device;

static AppLogger appLogger(TAG, config::LOG_EPSILON);

// Runs the control step: PID -> velocity -> gimbal.
static DeviceController controller(device, appLogger);

extern "C" void app_main()
{
    device.init();
    controller.begin(); // walks the working zone before the loop goes live

    ESP_LOGI(TAG, "ready - laser tracking, closed loop over UART");

    while (true)
    {
        device.tick();     // drain UART, service buttons, laser + buzzer timers
        controller.tick(); // rate-limited control step

        vTaskDelay(pdMS_TO_TICKS(config::LOOP_PERIOD_MS));
    }
}
