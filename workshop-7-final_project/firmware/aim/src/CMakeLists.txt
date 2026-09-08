# Single application component. drivers/ (peripheral wrappers), parts/ (the
# controllable device parts), tasks/ (one FreeRTOS task per file), transport/
# (the ITransport seam), ipc/ (queues + shared handles) and utils/ (generic
# helpers) are plain folders of header-only classes; adding them to INCLUDE_DIRS
# lets headers be included by their basename (e.g. #include "Ctrl.hpp"). The src
# root (".") holds main, Config, Pinout, StateMachine and ConfigStore.
FILE(GLOB_RECURSE app_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.c ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "." "drivers" "parts" "tasks" "transport" "ipc" "utils"
    REQUIRES
        driver
        esp_driver_i2c
        esp_timer
        nvs_flash
        espressif__led_strip
)
