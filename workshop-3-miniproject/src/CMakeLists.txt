# Single application component. drivers/ (peripheral wrappers), app/
# (application logic) and utils/ (generic helpers) are plain folders of
# header-only classes; adding them to INCLUDE_DIRS lets headers be included by
# their basename (e.g. #include "PWM.hpp").
FILE(GLOB_RECURSE app_sources ${CMAKE_CURRENT_SOURCE_DIR}/*.c ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "." "drivers" "app" "utils"
    REQUIRES
        driver
        esp_adc
        esp_timer
        espressif__led_strip
)
