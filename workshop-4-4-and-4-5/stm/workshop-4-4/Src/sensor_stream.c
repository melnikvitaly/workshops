#include "sensor_stream.h"
#include "bme280.h"
#include "ds1307.h"
#include "config.h"
#include "soft_timer.h"
#include "main.h"   // Error_Handler()

// Peripherals owned by CubeMX (main.c): ADC1 is configured for TIM2-triggered
// circular DMA (hdma_adc1, DMA2 Stream0), TIM2 paces the sampling, hi2c1 talks
// to the RTC and the BME280. This module only starts them and publishes the
// latest value of each sensor.
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim2;
extern I2C_HandleTypeDef hi2c1;

#define ADC_BUF_LEN LIGHT_ADC_BUF_LEN
#define ADC_HALVES  2u                 // DMA рапортує про буфер двома половинами
#define ADC_HALF    (ADC_BUF_LEN / ADC_HALVES)
#define ADC_MAX_RAW 4095u              // повна шкала 12-бітного АЦП
#define PERCENT_FULL 100u              // повна шкала у відсотках

// Circular DMA target for the ADC (filled by hdma_adc1). TIM2 clocks one
// conversion per tick; the DMA raises half/full interrupts as it wraps.
static uint16_t g_adc[ADC_BUF_LEN];

// Set by ISRs, consumed by SensorStream_Poll() (main loop).
static volatile uint8_t g_halfReady = 0;   // lower half [0, ADC_HALF) filled
static volatile uint8_t g_fullReady = 0;   // upper half [ADC_HALF, LEN) filled

// Published values. Written only by the poll (main loop), read by display_ui
// and telemetry_link — also main loop, so no synchronisation is needed beyond
// the volatile on the two ISR flags above.
static uint8_t  g_lightPercent = 0;
static uint16_t g_lightRaw     = 0;

static RtcTime  g_time      = {0};
static uint8_t  g_timeValid = 0;
static uint32_t g_lastRtcMs = 0;

static Bme280        g_bme;
static Bme280Reading g_env      = {0};
static uint8_t       g_envValid = 0;
static uint32_t      g_lastEnvMs = 0;

// --------------------------------------------------------------------------
// Init.
// --------------------------------------------------------------------------
void SensorStream_Init(void)
{
    // TIM2 (TRGO on Update Event), ADC1 (T2-triggered) and the circular ADC DMA
    // are all configured by CubeMX. Arm the DMA, then start TIM2 so its TRGO
    // paces the conversions. Same failure policy as the MX_*_Init functions in
    // main.c: an init-time HAL failure here is unrecoverable (no ADC feed for
    // the rest of the run), so it goes to Error_Handler() rather than silently
    // leaving g_adc unfed.
    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adc, ADC_BUF_LEN) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_TIM_Base_Start(&htim2) != HAL_OK)
    {
        Error_Handler();
    }

    // The BME280 gets the opposite policy: a missing sensor must not brick the
    // board, because the RTC, the light log, the display and the SPI link are
    // all perfectly usable without it. Failure here just leaves bme.ready at 0
    // and the poll retries once a second.
    BME280_Setup(&g_bme, &hi2c1);
    (void)BME280_Init(&g_bme);
}

// --------------------------------------------------------------------------
// Poll (main loop): publish whatever is ready.
// --------------------------------------------------------------------------
static void average_light(uint32_t start)
{
    uint32_t sum = 0;
    for (uint32_t i = start; i < start + ADC_HALF; ++i)
        sum += g_adc[i];
    uint32_t avg = sum / ADC_HALF;

    g_lightRaw     = (uint16_t)avg;
    g_lightPercent = (uint8_t)((avg * PERCENT_FULL) / ADC_MAX_RAW);
}

static void poll_rtc(void)
{
    if (!SoftTimer_Due(&g_lastRtcMs, RTC_SAMPLE_INTERVAL_MS))
        return;

    RtcTime t = {0};
    if (DS1307_ReadTime(&hi2c1, &t) == HAL_OK)
    {
        g_time      = t;
        g_timeValid = 1;
    }
    else
    {
        // Keep the last good reading in g_time: consumers show/send it as
        // "stale" (valid flag cleared) rather than jumping to 00:00:00.
        g_timeValid = 0;
    }
}

static void poll_env(void)
{
    if (!SoftTimer_Due(&g_lastEnvMs, ENV_SAMPLE_INTERVAL_MS))
        return;

    // Not up yet (absent at boot, hot-plugged, or dropped off the bus after a
    // failed read): one re-init attempt per period. BME280_Init pings both
    // possible addresses first, so an absent sensor costs a couple of short
    // NACKs rather than a full I2C timeout.
    if (!g_bme.ready)
    {
        g_envValid = 0;
        (void)BME280_Init(&g_bme);
        if (!g_bme.ready)
            return;
    }

    Bme280Reading r = {0};
    if (BME280_Read(&g_bme, &r) == HAL_OK)
    {
        g_env      = r;
        g_envValid = 1;
    }
    else
    {
        g_envValid = 0;   // BME280_Read already cleared bme.ready for us
    }
}

void SensorStream_Poll(void)
{
    if (g_halfReady) { g_halfReady = 0; average_light(0); }
    if (g_fullReady) { g_fullReady = 0; average_light(ADC_HALF); }

    poll_rtc();
    poll_env();
}

uint8_t SensorStream_LatestLightPercent(void)
{
    return g_lightPercent;
}

uint16_t SensorStream_LatestLightRaw(void)
{
    return g_lightRaw;
}

int SensorStream_LatestTime(RtcTime *out)
{
    if (out && g_timeValid)
        *out = g_time;
    return g_timeValid ? 1 : 0;
}

int SensorStream_LatestEnv(Bme280Reading *out)
{
    if (out && g_envValid)
        *out = g_env;
    return g_envValid ? 1 : 0;
}

// --------------------------------------------------------------------------
// ADC DMA callbacks (weak overrides; CubeMX doesn't generate these). The DMA
// IRQ handler itself lives in it.c (DMA2_Stream0_IRQHandler -> hdma_adc1). Only
// flags are touched here; the averaging happens in the poll.
// --------------------------------------------------------------------------
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
        g_halfReady = 1;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *hadc)
{
    if (hadc->Instance == ADC1)
        g_fullReady = 1;
}
