#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_log.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
static const char *TAG = "ADC_CAL";

// Attenuation and max measurable pin voltage per target.
// The attenuator scales the pin voltage down to the internal V_ref (~1.1V) before the ADC.
#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define ADC_ATTEN   ADC_ATTEN_DB_12
#define ADC_MAX_MV  3100
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#define ADC_ATTEN   ADC_ATTEN_DB_11
#define ADC_MAX_MV  2500
#else
#error "Unsupported target: only ESP32-S3 and ESP32-C3 are supported"
#endif
#define ADC_MAX_RAW   4095

static void adc_calibration_init(adc_unit_t unit, adc_channel_t channel,
                                  adc_atten_t atten, adc_cali_handle_t *out)
{
    adc_cali_curve_fitting_config_t cfg = {
        .unit_id  = unit,
        .chan     = channel,
        .atten    = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cfg, out));
    ESP_LOGI(TAG, "Curve-fitting calibration");
}

extern "C" void app_main()
{
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = ADC_UNIT_1,
        .clk_src = (adc_oneshot_clk_src_t)0,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle));

    adc_channel_t target_channel = ADC_CHANNEL_2;
    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten    = ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, target_channel, &chan_cfg));

    adc_cali_handle_t cali_handle = NULL;
    adc_calibration_init(ADC_UNIT_1, target_channel, ADC_ATTEN, &cali_handle);

    printf("\n%-4s | %-5s | %-14s | %-15s | %s\n",
           "#", "RAW", "Computed (mV)", "Calibrated (mV)", "Error (%)");
    printf("-----|-------|----------------|-----------------|----------\n");

    int n = 0;
    int raw = 0;
    int cali_mv = 0;

    while (1) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, target_channel, &raw));

        int computed_mv = (raw * ADC_MAX_MV) / ADC_MAX_RAW;

        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle, raw, &cali_mv));
        float error = (cali_mv > 0)
            ? ((float)(computed_mv - cali_mv) / cali_mv * 100.0f)
            : 0.0f;
        printf("%-4d | %-5d | %-14d | %-15d | %+.2f\n",
               ++n, raw, computed_mv, cali_mv, error);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
