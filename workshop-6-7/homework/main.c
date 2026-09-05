#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_adc/adc_oneshot.h"

static const char *TAG = "FRANKENSTEIN";

QueueHandle_t adc_queue;
adc_oneshot_unit_handle_t adc_handle;

void IRAM_ATTR bad_timer_isr_callback(void *arg) {
    int raw_adc_value = 0;
    
    adc_oneshot_read(adc_handle, ADC_CHANNEL_0, &raw_adc_value);
    
    esp_rom_printf("Raw ADC: %d\n", raw_adc_value);
    
    xQueueSend(adc_queue, &raw_adc_value, 0); 
}

void vDataProcessorTask(void *pvParameters) {
    ESP_LOGI(TAG, "Processor Task Started");
    
    while(1) {
        int adc_val;
        
        if (xQueueReceive(adc_queue, &adc_val, portMAX_DELAY)) {
            
            char json_packet[8192];
            sprintf(json_packet, "{\"sensor_id\": 42, \"adc_value\": %d, \"status\": \"OK\"}", adc_val);
            
            size_t packet_len = strlen(json_packet) + 1;
            char *tx_buffer = malloc(packet_len); 
            
            strcpy(tx_buffer, json_packet);
            
            bool send_success = (rand() % 10) > 3; 
            
            if (send_success) {
                ESP_LOGI(TAG, "Data sent via Wi-Fi: %d bytes", packet_len);
                free(tx_buffer);
            } else {
                ESP_LOGW(TAG, "Wi-Fi error! Retrying in next loop...");
            }
        }
    }
}

void app_main(void) {
    ESP_LOGW(TAG, "Booting Frankenstein System...");

    adc_queue = xQueueCreate(10, sizeof(int));
    
    adc_oneshot_unit_init_cfg_t init_config = { .unit_id = ADC_UNIT_1 };
    adc_oneshot_new_unit(&init_config, &adc_handle);
    adc_oneshot_chan_cfg_t config = { .bitwidth = ADC_BITWIDTH_DEFAULT, .atten = ADC_ATTEN_DB_12 };
    adc_oneshot_config_channel(adc_handle, ADC_CHANNEL_0, &config);

    const esp_timer_create_args_t timer_args = {
        .callback = &bad_timer_isr_callback,
        .name = "bad_isr_timer"
    };
    esp_timer_handle_t isr_timer;
    esp_timer_create(&timer_args, &isr_timer);
    esp_timer_start_periodic(isr_timer, 100000);

    xTaskCreate(vDataProcessorTask, "Processor", 2048, NULL, 5, NULL);
}