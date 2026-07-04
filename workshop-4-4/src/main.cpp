#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_log.h"

// Згідно з лекцією, SPI0/1 зарезервовані. Використовуємо апаратний SPI2 (FSPI)
#define SPI_MASTER_HOST  SPI2_HOST

// Визначаємо GPIO піни для ESP32-S3 DevKitC
// Оскільки ESP32-S3 дозволяє гнучко призначати піни, оберемо наступні:
#define PIN_NUM_MISO 13
#define PIN_NUM_MOSI 11
#define PIN_NUM_CLK  12
#define PIN_NUM_CS   10

// Статичний буфер у DMA-придатній внутрішній SRAM (розміщується лінкером, без heap).
// DMA_ATTR гарантує 32-бітне вирівнювання та розташування в SRAM, а не у флеш-пам'яті.
DMA_ATTR char tx_buffer[32];
static const char *TAG = "SPI_MASTER_ESP32S3";

void app_main(void)
{
    esp_err_t ret;
    spi_device_handle_t spi;

    ESP_LOGI(TAG, "Ініціалізація шини SPI...");

    // 1. Конфігурація ліній шини SPI (Bus Configuration)
    spi_bus_config_t buscfg = {0};
    buscfg.miso_io_num = PIN_NUM_MISO;
    buscfg.mosi_io_num = PIN_NUM_MOSI;
    buscfg.sclk_io_num = PIN_NUM_CLK;
    buscfg.quadwp_io_num = -1;  // Не використовуються для стандартного 4-провідного SPI
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 4092; // Максимальний обсяг для DMA транзакції згідно з лекції

    // 2. Конфігурація пристрою Slave на цій шині (Device Configuration)
    spi_device_interface_config_t devcfg = {0};
    devcfg.clock_speed_hz = 1 * 1000 * 1000;           // Швидкість тактування 1 МГц (детерміновані таймінги)
    devcfg.mode = 0;                                   // Mode 0 (CPOL=0, CPHA=0) — безпечний вибір за замовчуванням
    devcfg.spics_io_num = PIN_NUM_CS;                  // Апаратне керування лінією CS (опускається в LOW автоматично)
    devcfg.queue_size = 7;                             // Розмір черги транзакцій

    // 3. Ініціалізація шини SPI з автоматичним увімкненням DMA каналу (SPI_DMA_CH_AUTO)
    ret = spi_bus_initialize(SPI_MASTER_HOST, &buscfg, SPI_DMA_CH_AUTO);
    ESP_ERROR_CHECK(ret);

    // 4. Додавання Slave-пристрою (в нашому випадку плати STM32) до шини
    ret = spi_bus_add_device(SPI_MASTER_HOST, &devcfg, &spi);
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "SPI Master успішно ініціалізовано у Mode 0.");

    // 5. Заповнюємо статичний DMA-буфер (tx_buffer) текстом для відправки.
    // Буфер уже розміщено лінкером у DMA-придатній SRAM (див. DMA_ATTR вище), heap не потрібен.
    strcpy(tx_buffer, "Hello, STM!");

    // 6. Налаштування структури транзакції
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = strlen(tx_buffer) * 8; // Довжина транзакції обов'язково вказується в БІТАХ
    t.tx_buffer = tx_buffer;          // Вказівник на наш підготовлений буфер даних
    t.rx_buffer = NULL;               // В цьому завданні відповідь від STM нам не потрібна

    // 7. Виконання транзакцій у нескінченному циклі (асинхронний підхід через чергу)
    while (1) {
        ESP_LOGI(TAG, "Надсилання даних до STM: '%s' (%d біт)", tx_buffer, t.length);

        // Ставимо транзакцію в чергу — виклик повертається одразу, не чекаючи завершення передачі.
        // Дескриптор t мусить залишатися валідним (не виходити зі scope) до отримання результату.
        ret = spi_device_queue_trans(spi, &t, portMAX_DELAY);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Помилка постановки транзакції в чергу: %s", esp_err_to_name(ret));
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        // Тут CPU міг би виконувати іншу корисну роботу, поки DMA передає дані у фоні...

        // Очікуємо завершення транзакції та забираємо результат із черги.
        spi_transaction_t *ret_trans;
        ret = spi_device_get_trans_result(spi, &ret_trans, portMAX_DELAY);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Дані успішно надіслано по шині SPI!");
        } else {
            ESP_LOGE(TAG, "Помилка під час SPI транзакції: %s", esp_err_to_name(ret));
        }

        // Пауза між передачами
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}