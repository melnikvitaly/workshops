#include "telemetry_link.h"

#include "telemetry_packet.h"   // shared wire format (protocol/ — compiled by both ends)
#include "sensor_stream.h"
#include "config.h"
#include "soft_timer.h"
#include "main.h"               // SPI1_CS_Pin / SPI1_CS_GPIO_Port, Error_Handler()

// SPI1 is configured as a master by CubeMX (MX_SPI1_Init: mode 0, MSB first,
// prescaler /16 -> 1 MHz off the 16 MHz APB2, software NSS). The CS pin is a
// plain GPIO output brought up by MX_GPIO_Init and driven here.
extern SPI_HandleTypeDef hspi1;

// ---------------------------------------------------------------------------
// Frame buffer and counters.
// ---------------------------------------------------------------------------
// One frame is built in place and sent; there is no queue behind it. With a
// synchronous link there is nothing a queue could buy: the transmit either
// completes before this function returns or fails, and by the time the next
// period comes round the sensors have fresh values anyway. Holding a stale
// frame back to retry would send old readings with a new sequence number,
// which is worse than the gap.
static TelemetryFrame g_frame;

static uint32_t g_seq          = 0;   // incremented per frame built (1-based on the wire)
static uint32_t g_framesSent   = 0;
static uint32_t g_framesFailed = 0;
static uint32_t g_lastSendMs   = 0;

// ---------------------------------------------------------------------------
// Activity LED (on-board PC13, active-low on the STM32F401CCUx "black pill").
// Lit for the duration of the transmit. At one 256 us frame per second that is
// a short blink, which is exactly the point: a steady dark LED means the link
// stopped, without needing a console.
// ---------------------------------------------------------------------------
#define ACT_LED_PORT   GPIOC
#define ACT_LED_PIN    GPIO_PIN_13
#define ACT_LED_ON()   HAL_GPIO_WritePin(ACT_LED_PORT, ACT_LED_PIN, GPIO_PIN_RESET)
#define ACT_LED_OFF()  HAL_GPIO_WritePin(ACT_LED_PORT, ACT_LED_PIN, GPIO_PIN_SET)

// A 256 us blink is invisible, so stretch it: the LED stays lit for
// ACT_BLINK_MS after the transmit and is released by the poll. Still no
// blocking — the release is just a timer check on the way through.
#define ACT_BLINK_MS   50u

static uint32_t g_ledOnMs = 0;
static uint8_t  g_ledOn   = 0;

static void act_led_init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef gi = {0};
    gi.Pin   = ACT_LED_PIN;
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(ACT_LED_PORT, &gi);
    ACT_LED_OFF();
}

// ---------------------------------------------------------------------------
// Chip select.
// ---------------------------------------------------------------------------
// Software NSS: SPI_NSS_SOFT leaves PA4 to us, so CS assertion is an ordinary
// GPIO write. Hardware NSS output would toggle CS per *byte* on the F4 rather
// than per transfer, which would break the "one CS assertion = one frame"
// framing the receiver relies on.
static void cs_select(void)   { HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_RESET); }
static void cs_deselect(void) { HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_SET); }

// ---------------------------------------------------------------------------
// Frame assembly.
// ---------------------------------------------------------------------------
static void build_frame(void)
{
    TelemetryPayload *p = &g_frame.payload;
    RtcTime           t;
    Bme280Reading     env;

    // Start from the previous frame's contents rather than zeroing: a sensor
    // that failed this cycle then keeps its last known value on the wire while
    // its validity flag is cleared, so the receiver can show "23.4 (stale)"
    // instead of a plausible-looking 0.0. Only the flags are rebuilt.
    p->flags = 0;

    if (SensorStream_LatestTime(&t))
    {
        p->year   = t.year;
        p->month  = t.month;
        p->day    = t.date;
        p->hour   = t.hours;
        p->minute = t.minutes;
        p->second = t.seconds;
        p->flags |= TELEMETRY_FLAG_TIME_VALID;
    }

    if (SensorStream_LatestEnv(&env))
    {
        // The BME280 driver already reports in exactly the wire's units
        // (°C*100, %RH*100, Pa), so there is no conversion here to get wrong.
        p->tempC100    = env.tempC100;
        p->humidity100 = env.humidity100;
        p->pressurePa  = env.pressurePa;
        p->flags      |= TELEMETRY_FLAG_ENV_VALID;
    }

    // Light has no failure mode to report: the ADC free-runs on DMA, so there
    // is always a value. The flag is sent anyway so the receiver's handling of
    // all three sources stays uniform.
    p->lightPct = SensorStream_LatestLightPercent();
    p->lightRaw = SensorStream_LatestLightRaw();
    p->flags   |= TELEMETRY_FLAG_LIGHT_VALID;

    p->uptimeMs = HAL_GetTick();
    p->seq      = ++g_seq;

    Telemetry_Seal(&g_frame);   // magic/version/length + CRC over everything before it
}

static void send_frame(void)
{
    ACT_LED_ON();
    g_ledOnMs = HAL_GetTick();
    g_ledOn   = 1;

    cs_select();
    HAL_StatusTypeDef ret = HAL_SPI_Transmit(&hspi1, (uint8_t *)&g_frame,
                                             TELEMETRY_FRAME_SIZE, TELEMETRY_SPI_TIMEOUT);
    cs_deselect();

    if (ret == HAL_OK)
        ++g_framesSent;
    else
        ++g_framesFailed;
}

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------
void TelemetryLink_Init(void)
{
    // MX_GPIO_Init already parked CS high before SPI1 came up; do it again so
    // this module is correct on its own if the call order ever changes.
    cs_deselect();
    act_led_init();
}

void TelemetryLink_Poll(void)
{
    if (g_ledOn && (HAL_GetTick() - g_ledOnMs) >= ACT_BLINK_MS)
    {
        ACT_LED_OFF();
        g_ledOn = 0;
    }

    if (!SoftTimer_Due(&g_lastSendMs, TELEMETRY_PERIOD_MS))
        return;

    build_frame();
    send_frame();
}

void TelemetryLink_GetStats(TelemetryLinkStats *out)
{
    if (!out)
        return;
    out->framesSent   = g_framesSent;
    out->framesFailed = g_framesFailed;
    out->lastSeq      = g_seq;
}
