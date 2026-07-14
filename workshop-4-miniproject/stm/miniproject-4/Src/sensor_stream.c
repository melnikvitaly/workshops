#include "sensor_stream.h"
#include "log_emission.h"
#include "ds1307.h"
#include "config.h"

// Peripherals owned by CubeMX (main.c): ADC1 is configured for TIM2-triggered
// circular DMA (hdma_adc1, DMA2 Stream0), TIM2 paces the sampling, hi2c1 talks
// to the RTC. This module only starts them and turns the data into log entries.
extern ADC_HandleTypeDef hadc1;
extern TIM_HandleTypeDef htim2;
extern I2C_HandleTypeDef hi2c1;

#define ADC_BUF_LEN LIGHT_ADC_BUF_LEN
#define ADC_HALF    (ADC_BUF_LEN / 2)
#define ADC_MAX_RAW 4095u

// Circular DMA target for the ADC (filled by hdma_adc1). TIM2 clocks one
// conversion per tick; the DMA raises half/full interrupts as it wraps.
static uint16_t g_adc[ADC_BUF_LEN];

// Set by ISRs, consumed by SensorStream_Poll() (main loop).
static volatile uint8_t g_halfReady = 0;   // lower half [0, ADC_HALF) filled
static volatile uint8_t g_fullReady = 0;   // upper half [ADC_HALF, LEN) filled
static volatile uint8_t g_lightPercent = 0;

static uint32_t g_lastRtcMs = 0;           // last RTC read tick (software timer)

// --------------------------------------------------------------------------
// Init.
// --------------------------------------------------------------------------
void SensorStream_Init(void)
{
    // TIM2 (TRGO on Update Event), ADC1 (T2-triggered) and the circular ADC DMA
    // are all configured by CubeMX. Arm the DMA, then start TIM2 so its TRGO
    // paces the conversions.
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)g_adc, ADC_BUF_LEN);
    HAL_TIM_Base_Start(&htim2);
}

// --------------------------------------------------------------------------
// Poll (main loop): turn ready data into log entries.
// --------------------------------------------------------------------------
static void emit_light(uint32_t start)
{
    uint32_t sum = 0;
    for (uint32_t i = start; i < start + ADC_HALF; ++i)
        sum += g_adc[i];
    uint32_t avg = sum / ADC_HALF;
    uint8_t  pct = (uint8_t)((avg * 100u) / ADC_MAX_RAW);

    g_lightPercent = pct;
    LogEmission_AddEntry("LGHT", LOGVT_U8, &pct, 1);
}

static void emit_rtc(void)
{
    RtcTime t = {0};
    if (DS1307_ReadTime(&hi2c1, &t) != HAL_OK)
        return;
    LogDateTime dt = {
        .year = t.year, .month = t.month, .date = t.date,
        .hours = t.hours, .minutes = t.minutes, .seconds = t.seconds,
    };
    LogEmission_AddDateTime("TIME", &dt);
}

void SensorStream_Poll(void)
{
    if (g_halfReady) { g_halfReady = 0; emit_light(0); }
    if (g_fullReady) { g_fullReady = 0; emit_light(ADC_HALF); }

    if (g_lastRtcMs == 0 || HAL_GetTick() - g_lastRtcMs >= RTC_STREAM_INTERVAL_MS)
    {
        g_lastRtcMs = HAL_GetTick();
        emit_rtc();
    }
}

uint8_t SensorStream_LatestLightPercent(void)
{
    return g_lightPercent;
}

// --------------------------------------------------------------------------
// ADC DMA callbacks (weak overrides; CubeMX doesn't generate these). The DMA
// IRQ handler itself lives in it.c (DMA2_Stream0_IRQHandler -> hdma_adc1). Only
// flags are touched here; the I2C read and log writes happen in the poll.
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
