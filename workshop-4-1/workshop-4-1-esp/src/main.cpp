#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

// ESP32-C3 has only UART0 (console) and UART1
#define UART_NUM UART_NUM_1
#define TXD_PIN 4
#define RXD_PIN 3
#define BUF_SIZE 1024
#define LED_PIN 8

static const char *TAG = "APP";

// Reads lines from UART_NUM and toggles the built-in LED on LED_ON/LED_OFF.
static void uart_rx_task(void *arg)
{
    uint8_t data[128];
    char line[64];
    int line_len = 0;

    while (1)
    {
        int len = uart_read_bytes(UART_NUM, data, sizeof(data), pdMS_TO_TICKS(100));
        for (int i = 0; i < len; i++)
        {
            char c = (char)data[i];
            if (c == '\n' || c == '\r')
            {
                if (line_len > 0)
                {
                    line[line_len] = '\0';
                    if (strcmp(line, "LED_ON") == 0)
                    {
                        gpio_set_level((gpio_num_t)LED_PIN, 1);
                        ESP_LOGI(TAG, "Received: LED_ON");
                    }
                    else if (strcmp(line, "LED_OFF") == 0)
                    {
                        gpio_set_level((gpio_num_t)LED_PIN, 0);
                        ESP_LOGI(TAG, "Received: LED_OFF");
                    }
                    line_len = 0;
                }
            }
            else if (line_len < (int)sizeof(line) - 1)
            {
                line[line_len++] = c;
            }
            else
            {
                // Overflow: drop the line being assembled.
                line_len = 0;
            }
        }
    }
}

extern "C" void app_main(void)
{

    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};

    uart_driver_install(UART_NUM, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_param_config(UART_NUM, &uart_config);
    uart_set_pin(UART_NUM, TXD_PIN, RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Initialize GPIO for LED
    gpio_reset_pin((gpio_num_t)LED_PIN);
    gpio_set_direction((gpio_num_t)LED_PIN, GPIO_MODE_OUTPUT);

    // Blink LED 3 times on startup
    for (int i = 0; i < 3; i++)
    {
        gpio_set_level((gpio_num_t)LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level((gpio_num_t)LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(200));
    }

    // Listen for LED_ON/LED_OFF on the same UART and drive the built-in LED.
    xTaskCreate(uart_rx_task, "uart_rx_task", 4096, NULL, 10, NULL);

    int state = 0;

    while (1)
    {
        const char *cmd = (state == 0) ? "LED_ON\n" : "LED_OFF\n";
        uart_write_bytes(UART_NUM, cmd, strlen(cmd));
        ESP_LOGI(TAG, "Sent: %s", cmd);

        state = !state;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}