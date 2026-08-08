#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "PID_SMART_FAN";

#define THERMISTOR_ADC_CHAN  ADC_CHANNEL_3
#define MOTOR_PWM_GPIO       GPIO_NUM_18
#define MOTOR_PWM_FREQ_HZ    5000
#define MOTOR_RESOLUTION     LEDC_TIMER_10_BIT
#define MAX_DUTY_CYCLE       1023
#define MOTOR_MIN_PWM        200

typedef struct {
    float Kp;
    float Ki;
    float Kd;
    float integral;
    float prev_pv;
    float out_min;
    float out_max;
} pid_controller_t;

pid_controller_t fan_pid = {
    .Kp = 0.0f,
    .Ki = 0.0f,
    .Kd = 0.0f,
    .integral = 0.0f,
    .prev_pv = 0.0f,
    .out_min = 0.0f,
    .out_max = (float)MAX_DUTY_CYCLE
};

const float SETPOINT_TEMP = 28.0f;

adc_oneshot_unit_handle_t init_adc() {
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN_DB_12,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, THERMISTOR_ADC_CHAN, &config));
    return adc_handle;
}

void init_motor_pwm() {
    ledc_timer_config_t timer_cfg = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .timer_num       = LEDC_TIMER_0,
        .duty_resolution = MOTOR_RESOLUTION,
        .freq_hz         = MOTOR_PWM_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_cfg));

    ledc_channel_config_t ch_cfg = {
        .gpio_num   = MOTOR_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 0,
        .hpoint     = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_cfg));
}

float read_temperature_celsius(adc_oneshot_unit_handle_t adc) {
    int adc_raw = 0;
    adc_oneshot_read(adc, THERMISTOR_ADC_CHAN, &adc_raw);

    if (adc_raw == 0 || adc_raw == 4095) return 25.0f;

    float R_series = 10000.0f;
    float R_thermistor = R_series * (4095.0f / (float)adc_raw - 1.0f);

    float nominal_resistance = 10000.0f;
    float nominal_temp = 25.0f + 273.15f;
    float b_coefficient = 3950.0f;

    float steinhart = R_thermistor / nominal_resistance;
    steinhart = log(steinhart);
    steinhart /= b_coefficient;
    steinhart += 1.0f / nominal_temp;
    steinhart = 1.0f / steinhart;
    steinhart -= 273.15f;

    return steinhart;
}

void pid_control_task(void *pvParameters) {
    adc_oneshot_unit_handle_t adc = (adc_oneshot_unit_handle_t)pvParameters;

    const TickType_t xFrequency = pdMS_TO_TICKS(50);
    TickType_t xLastWakeTime = xTaskGetTickCount();
    int64_t last_time = esp_timer_get_time();

    float filtered_temp = read_temperature_celsius(adc);
    fan_pid.prev_pv = filtered_temp;

    while (1) {
        float raw_temp = read_temperature_celsius(adc);
        filtered_temp = (0.1f * raw_temp) + (0.9f * filtered_temp);

        int64_t now = esp_timer_get_time();
        float dt = (now - last_time) / 1000000.0f;
        last_time = now;
        if (dt <= 0.00001f) dt = 0.05f;

        float error = filtered_temp - SETPOINT_TEMP;

        float derivative = -(filtered_temp - fan_pid.prev_pv) / dt;
        fan_pid.prev_pv = filtered_temp;

        float output_pre = (fan_pid.Kp * error) + (fan_pid.Ki * fan_pid.integral) + (fan_pid.Kd * derivative);

        if ((output_pre < fan_pid.out_max || error < 0.0f) &&
            (output_pre > fan_pid.out_min || error > 0.0f)) {
            fan_pid.integral += error * dt;
        }

        if (fan_pid.Ki > 0.0f) {
            float integral_limit = fan_pid.out_max / fan_pid.Ki;
            if (fan_pid.integral > integral_limit) fan_pid.integral = integral_limit;
            if (fan_pid.integral < -integral_limit) fan_pid.integral = -integral_limit;
        }

        float output = (fan_pid.Kp * error) + (fan_pid.Ki * fan_pid.integral) + (fan_pid.Kd * derivative);

        if (output > fan_pid.out_max) output = fan_pid.out_max;
        if (output < fan_pid.out_min) output = fan_pid.out_min;

        uint32_t duty = 0;
        if (output >= MOTOR_MIN_PWM) {
            duty = (uint32_t)output;
        }

        ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
        ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);

        printf("SP:%.2f PV:%.2f PWM:%lu\n", SETPOINT_TEMP, filtered_temp, duty);

        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting PID test stand...");
    adc_oneshot_unit_handle_t adc_handle = init_adc();
    init_motor_pwm();

    xTaskCreate(pid_control_task, "pid_task", 4096, (void*)adc_handle, 5, NULL);
} (ed