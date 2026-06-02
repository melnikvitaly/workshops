#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_timer.h>
#include <esp_log.h>
#include "hardware/PWM.hpp"
#include "hardware/Buzzer.hpp"
#include "Melody.hpp"
#include "MelodyPlayer.hpp"

static constexpr gpio_num_t BUZZER_PIN = GPIO_NUM_8;
static constexpr uint64_t   TICK_US    = 50'000; // 50 ms

static PWM          buzzerPwm(BUZZER_PIN, LEDC_CHANNEL_0, LEDC_TIMER_0, Notes::C4);
static Buzzer       buzzer(buzzerPwm);
static MelodyPlayer player(MELODIES, MELODY_COUNT);

static const char* TAG = "PLAYER";

static void onTick(void*)
{
    const char* before = player.currentName();
    player.tick(buzzer);
    if (player.currentName() != before)
        ESP_LOGI(TAG, "Now playing: %s", player.currentName());
}

extern "C" void app_main()
{
    buzzer.init();
    ESP_LOGI(TAG, "Starting: %s", player.currentName());

    esp_timer_create_args_t args = {
        .callback        = onTick,
        .arg             = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "melody",
    };
    esp_timer_handle_t timer;
    ESP_ERROR_CHECK(esp_timer_create(&args, &timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer, TICK_US));

    while (1)
        vTaskDelay(portMAX_DELAY);
}
