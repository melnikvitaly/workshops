#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>

#include "Config.hpp"
#include "ViewPort.hpp"
#include "Device.hpp"
#include "DeviceController.hpp"
#include "AppLogger.hpp"

static const char *TAG = "APP";

// Holds all parts and their drivers. Some parts are just represented by driver directly
static Device          device;

static AppLogger       appLogger(TAG, config::LOG_EPSILON);

// Drives device based on inputs
static DeviceController controller(device, appLogger);

extern "C" void app_main()
{
    device.init();   // brings up every owned part + wires the control buttons

    ESP_LOGI(TAG, "ready");
  
    while (true)
    {
        device.tick();        
        controller.tick();

        vTaskDelay(pdMS_TO_TICKS(config::LOOP_PERIOD_MS));
    }
}
