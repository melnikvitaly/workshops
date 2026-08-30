#pragma once
#include <cstdint>

#include <driver/gpio.h>
#include <driver/i2c_types.h>
#include <esp_adc/adc_oneshot.h>
#include <freertos/FreeRTOS.h>

struct Config
{
#pragma region I2C / BME280
    // Both are strapping pins, and on a C3 mini both are taken: GPIO8 drives the
    // on-board LED, GPIO9 is the BOOT button. External 4.7k pull-ups to 3V3 are
    // required so the lines idle high through reset.
    static constexpr i2c_port_num_t I2C_PORT = 0;
    static constexpr gpio_num_t I2C_SDA_PIN = GPIO_NUM_8;
    static constexpr gpio_num_t I2C_SCL_PIN = GPIO_NUM_9;
    static constexpr uint32_t I2C_FREQ_HZ = 100000; // 100 kHz tolerates weak pull-ups
    static constexpr int I2C_TIMEOUT_MS = 100;
    static constexpr uint8_t I2C_GLITCH_IGNORE_CNT = 7; // value the IDF docs recommend

    // 0x76 = SDO tied low, which is how the module answered in workshop 4-4/4-5.
    static constexpr uint16_t BME280_ADDR = 0x76;
#pragma endregion

#pragma region LDR / ADC1
    // ADC1 keeps working while Wi-Fi is on; ADC2 does not. GPIO0 = ADC1_CH0.
    static constexpr adc_channel_t LDR_CHANNEL = ADC_CHANNEL_0;
    static constexpr adc_bitwidth_t LDR_BITWIDTH = ADC_BITWIDTH_12;
    static constexpr adc_atten_t LDR_ATTEN = ADC_ATTEN_DB_12; // widest span, ~0..3.1 V
    static constexpr uint8_t LDR_SAMPLES = 16;

    // Hand-measured window of this divider — an LDR never swings across the full
    // ADC span. Re-measure if the readings never reach either end.
    static constexpr int32_t LDR_RAW_MIN = 200;
    static constexpr int32_t LDR_RAW_MAX = 3800;
#pragma endregion

#pragma region TASKS
    static constexpr uint32_t TEMP_PERIOD_MS = 2000; // temperature moves slowly
    static constexpr uint32_t LIGHT_PERIOD_MS = 500;

    // Consumer above both producers: a burst from two sensors must not sit in the
    // queue long enough to push a producer into its send timeout.
    static constexpr UBaseType_t CONSUMER_PRIORITY = 4;
    static constexpr UBaseType_t TEMP_PRIORITY = 3;
    static constexpr UBaseType_t LIGHT_PRIORITY = 3;

    static constexpr uint32_t TASK_STACK_SIZE = 3072; // bytes, as ESP-IDF counts them
#pragma endregion

#pragma region QUEUE
    static constexpr UBaseType_t QUEUE_LENGTH = 8;
    static constexpr uint32_t QUEUE_SEND_TIMEOUT_MS = 10; // bounded wait: a producer never blocks forever
#pragma endregion
};
