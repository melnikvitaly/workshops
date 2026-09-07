# Single application component. drivers/ (peripheral wrappers), parts/ (the
# controllable device parts), inputs/ (the Input sources) and utils/ (generic
# helpers) are plain folders of header-only classes; adding them to INCLUDE_DIRS
# lets headers be included by their basename (e.g. #include "PWM.hpp"). The src
# root (".") holds Device, DeviceController and the rest of the core app logic.
FILE(GLOB_RECURSE app_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.c ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "." "drivers" "parts" "inputs" "utils"
    REQUIRES
        driver
        esp_timer
        espressif__led_strip
)
