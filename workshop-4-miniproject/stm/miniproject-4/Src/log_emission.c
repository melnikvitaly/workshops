#include "log_emission.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Protocol constants — mirror of espc3/src/LogProtocol.hpp. KEEP IN SYNC.
// One-way stream: [18-byte header][payload 0..255][crc16], little-endian.
// ---------------------------------------------------------------------------
#define LP_MAGIC        0xA5

// Status byte: low nibble = kind, high bits = flags.
#define SC_ENTRY        0x00
#define SC_NOENTRY      0x01
#define FLAG_OVERFLOW   0x80

#define LP_HEADER_SIZE  18
#define LP_CRC_SIZE     2
#define LP_OBJID_LEN    4
#define LP_MAX_PACKET   (LP_HEADER_SIZE + 255 + LP_CRC_SIZE)  // 275

// CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF), identical to the master's.
static uint16_t crc16(const uint8_t *d, uint16_t n)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < n; ++i)
    {
        crc ^= (uint16_t)d[i] << 8;
        for (int b = 0; b < 8; ++b)
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                 : (uint16_t)(crc << 1);
    }
    return crc;
}

// Build one packet into `out` (must hold up to LP_MAX_PACKET bytes). Fields are
// written little-endian with memcpy (both ends are little-endian). Returns the
// packet length. `objId` may be NULL for NoEntry (filled with zeros).
static uint16_t build_packet(uint8_t *out, uint8_t status, uint32_t seq,
                             uint16_t dropped, const char *objId,
                             uint8_t vtype, const uint8_t *payload, uint8_t plen)
{
    uint32_t ts = HAL_GetTick();
    out[0] = LP_MAGIC;
    out[1] = status;
    memcpy(out + 2, &seq, 4);
    memcpy(out + 6, &dropped, 2);
    memcpy(out + 8, &ts, 4);
    if (objId) memcpy(out + 12, objId, LP_OBJID_LEN);
    else       memset(out + 12, 0, LP_OBJID_LEN);
    out[16] = vtype;
    out[17] = plen;
    if (plen) memcpy(out + LP_HEADER_SIZE, payload, plen);
    uint16_t crc = crc16(out, (uint16_t)(LP_HEADER_SIZE + plen));
    memcpy(out + LP_HEADER_SIZE + plen, &crc, 2);
    return (uint16_t)(LP_HEADER_SIZE + plen + LP_CRC_SIZE);
}

// ---------------------------------------------------------------------------
// Stream ring. A byte buffer that free-running circular DMA feeds to MISO. The
// producer appends packets at g_write, wrapping; the DMA read position advances
// only as the master clocks bytes out. It is pre-filled with NoEntry packets so
// an idle slave still presents valid frames (the master infers presence from
// them). Overwriting bytes the master has not yet read is the sole loss path and
// is counted in g_dropped, reported to the master.
// ---------------------------------------------------------------------------
#define STREAM_CAP 2048

static uint8_t           g_stream[STREAM_CAP];
static volatile uint16_t g_write   = 0;  // next byte position to write
static volatile uint32_t g_nextSeq = 1;  // id for the next ENTRY (0 = NoEntry)
static volatile uint16_t g_dropped = 0;  // bytes-overflowed events (saturating)

// SPI1 and its TX DMA are configured entirely by CubeMX: MX_SPI1_Init (slave,
// mode 0, hardware NSS, PA4..PA7) and MX_DMA_Init + HAL_SPI_MspInit set up the
// circular DMA2 Stream3 handle `hdma_spi1_tx` and link it to hspi1. This module
// just starts that circular DMA on the ring and drives the stream.
extern SPI_HandleTypeDef hspi1;
extern DMA_HandleTypeDef hdma_spi1_tx;

// Current DMA read index (bytes already handed to the SPI shift path).
static uint16_t stream_read_index(void)
{
    uint16_t ndtr = (uint16_t)__HAL_DMA_GET_COUNTER(&hdma_spi1_tx);
    return (uint16_t)(STREAM_CAP - ndtr);
}

// ---------------------------------------------------------------------------
// SPI-activity LED. Blinks while the master is clocking the stream.
//
// The TX path is free-running circular DMA with no per-byte/-packet interrupt,
// so "bytes moved" can't be caught in a callback — instead we watch the DMA read
// index advance. Every byte the master clocks out of us on MISO is one it
// simultaneously clocks into us on MOSI, so a change in the read position means
// SPI traffic in both directions (RX and TX at once). The LED is lit when traffic
// is seen and released a short time after it stops, giving a visible activity
// blink. Poll from the main loop (see LogEmission_ActivityPoll); it uses only
// GPIO writes and HAL_GetTick, so it is also safe to call from an ISR.
//
// PC13 is the on-board LED of the STM32F401CCUx ("black pill") board and is
// unused elsewhere here; it is active-low (drive the pin low to light it). The
// pin is configured in LogEmission_Init so the whole SPI-slave feature stays
// self-contained in this module.
// ---------------------------------------------------------------------------
#define ACT_LED_PORT   GPIOC
#define ACT_LED_PIN    GPIO_PIN_13
#define ACT_LED_ON()   HAL_GPIO_WritePin(ACT_LED_PORT, ACT_LED_PIN, GPIO_PIN_RESET)
#define ACT_LED_OFF()  HAL_GPIO_WritePin(ACT_LED_PORT, ACT_LED_PIN, GPIO_PIN_SET)
#define ACT_BLINK_MS   25u   // keep the LED lit this long after the last activity

static uint16_t g_lastReadIdx = 0;  // DMA read index sampled at the previous poll
static uint32_t g_lastActMs   = 0;  // tick of the last observed SPI activity
static uint8_t  g_ledOn       = 0;  // current LED state (1 = lit)

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
    g_lastReadIdx = stream_read_index();
}

// ---------------------------------------------------------------------------
// SPI RX (MOSI) capture — debug only. See LOGEMIT_DEBUG_SPI in the header.
// Normally the slave never reads MOSI (the protocol is one-way MISO). For pin
// bring-up we drain SPI1_RX with a second free-running circular DMA so the bytes
// the master clocks in on MOSI land in RAM and can be watched in a debugger.
// SPI1_RX is DMA2 Stream2 / Channel 3 on the STM32F401 (Stream3 = TX, Stream0 =
// ADC), so it doesn't clash with the TX DMA or the ADC DMA. Polling mode, no IRQ.
// ---------------------------------------------------------------------------
#if LOGEMIT_DEBUG_SPI
#define SPI_RX_DEBUG_LEN 64

static uint8_t           g_spiRxDebug[SPI_RX_DEBUG_LEN];  // circular MOSI capture
static DMA_HandleTypeDef hdma_spi1_rx;                    // hand-configured (not CubeMX)
static volatile uint8_t  g_spiRxLast  = 0;               // last byte seen on MOSI
static volatile uint32_t g_spiRxCount = 0;               // bytes captured since boot
static uint16_t          g_rxLastIdx  = 0;               // DMA write index at last poll

static void spi_rx_debug_init(void)
{
    __HAL_RCC_DMA2_CLK_ENABLE();
    hdma_spi1_rx.Instance                 = DMA2_Stream2;
    hdma_spi1_rx.Init.Channel             = DMA_CHANNEL_3;
    hdma_spi1_rx.Init.Direction           = DMA_PERIPH_TO_MEMORY;
    hdma_spi1_rx.Init.PeriphInc           = DMA_PINC_DISABLE;
    hdma_spi1_rx.Init.MemInc              = DMA_MINC_ENABLE;
    hdma_spi1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_spi1_rx.Init.MemDataAlignment    = DMA_MDATAALIGN_BYTE;
    hdma_spi1_rx.Init.Mode                = DMA_CIRCULAR;
    hdma_spi1_rx.Init.Priority            = DMA_PRIORITY_LOW;
    hdma_spi1_rx.Init.FIFOMode            = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_spi1_rx);
    HAL_DMA_Start(&hdma_spi1_rx, (uint32_t)&hspi1.Instance->DR,
                  (uint32_t)g_spiRxDebug, SPI_RX_DEBUG_LEN);
    SET_BIT(hspi1.Instance->CR2, SPI_CR2_RXDMAEN);
}

// Refresh the "last byte / count" snapshot from the RX DMA write position.
static void spi_rx_debug_poll(void)
{
    uint16_t ndtr = (uint16_t)__HAL_DMA_GET_COUNTER(&hdma_spi1_rx);
    uint16_t widx = (uint16_t)(SPI_RX_DEBUG_LEN - ndtr);   // next slot the DMA will fill
    if (widx != g_rxLastIdx)
    {
        uint16_t last = (uint16_t)((widx + SPI_RX_DEBUG_LEN - 1) % SPI_RX_DEBUG_LEN);
        g_spiRxLast   = g_spiRxDebug[last];
        g_spiRxCount += (uint16_t)((widx - g_rxLastIdx + SPI_RX_DEBUG_LEN) % SPI_RX_DEBUG_LEN);
        g_rxLastIdx   = widx;
    }
}

uint32_t LogEmission_DebugRx(uint8_t *lastByte)
{
    if (lastByte) *lastByte = g_spiRxLast;
    return g_spiRxCount;
}
#endif // LOGEMIT_DEBUG_SPI

// Append n bytes into the ring, wrapping. If this overtakes the master's read
// position we are overwriting unread data — count it as an overflow.
static void stream_write(const uint8_t *p, uint16_t n)
{
    if (n == 0 || n > STREAM_CAP)
        return;

    uint16_t r        = stream_read_index();
    uint16_t inflight = (uint16_t)((g_write - r + STREAM_CAP) % STREAM_CAP);
    if ((uint32_t)inflight + n >= STREAM_CAP && g_dropped != 0xFFFF)
        g_dropped++;

    uint16_t first = (uint16_t)(STREAM_CAP - g_write);
    if (first >= n)
    {
        memcpy(&g_stream[g_write], p, n);
    }
    else
    {
        memcpy(&g_stream[g_write], p, first);
        memcpy(&g_stream[0], p + first, (uint16_t)(n - first));
    }
    g_write = (uint16_t)((g_write + n) % STREAM_CAP);
}

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------
void LogEmission_AddEntry(const char objectId[LOGEMIT_OBJID_LEN],
                          uint8_t valueType,
                          const void *payload, uint8_t payloadLen)
{
    uint8_t  pkt[LP_MAX_PACKET];
    uint8_t  status = (uint8_t)(SC_ENTRY | (g_dropped ? FLAG_OVERFLOW : 0));
    uint32_t seq    = g_nextSeq++;
    uint16_t len    = build_packet(pkt, status, seq, g_dropped, objectId,
                                   valueType, (const uint8_t *)payload, payloadLen);
    stream_write(pkt, len);
}

void LogEmission_AddU32(const char objectId[LOGEMIT_OBJID_LEN], uint32_t value)
{
    LogEmission_AddEntry(objectId, LOGVT_U32, &value, sizeof(value));
}

void LogEmission_AddDateTime(const char objectId[LOGEMIT_OBJID_LEN],
                             const LogDateTime *dt)
{
    // 6 raw bytes (year..seconds), typed VT_DATETIME. Treated like any sensor.
    LogEmission_AddEntry(objectId, LOGVT_DATETIME, dt, sizeof(*dt));
}

void LogEmission_AddText(const char objectId[LOGEMIT_OBJID_LEN], const char *text)
{
    size_t n = strlen(text);
    if (n > 255)
        n = 255;
    LogEmission_AddEntry(objectId, LOGVT_STR, text, (uint8_t)n);
}

void LogEmission_ActivityPoll(void)
{
#if LOGEMIT_DEBUG_SPI
    spi_rx_debug_poll();   // refresh the captured-MOSI debug snapshot
#endif

    uint16_t r = stream_read_index();
    if (r != g_lastReadIdx)             // master clocked bytes since the last poll
    {
        g_lastReadIdx = r;
        g_lastActMs   = HAL_GetTick();
        if (!g_ledOn) { ACT_LED_ON(); g_ledOn = 1; }
    }
    else if (g_ledOn && (HAL_GetTick() - g_lastActMs) >= ACT_BLINK_MS)
    {
        ACT_LED_OFF();                  // no traffic for ACT_BLINK_MS -> release
        g_ledOn = 0;
    }
}

void LogEmission_Init(void)
{
    // SPI1 (MX_SPI1_Init) and its circular TX DMA (MX_DMA_Init + HAL_SPI_MspInit,
    // handle `hdma_spi1_tx`) are already initialised by CubeMX before this call.

#if LOGEMIT_DEBUG_SPI
    // Debug: fill the whole ring with a repeating 0x00..0xFF ramp so MISO shows an
    // obvious incrementing sawtooth on a scope (no protocol framing — the master
    // will not parse packets while this is on). See LOGEMIT_DEBUG_SPI in header.
    for (uint16_t i = 0; i < STREAM_CAP; ++i)
        g_stream[i] = (uint8_t)i;
    g_write = 0;
#else
    // Pre-fill the whole ring with back-to-back NoEntry packets so the stream is
    // valid from the first clock. Any trailing bytes that cannot hold a full
    // packet are zeroed (non-MAGIC: the master skips them while resyncing).
    uint16_t w = 0;
    while (w + (LP_HEADER_SIZE + LP_CRC_SIZE) <= STREAM_CAP)
    {
        uint8_t  pkt[LP_HEADER_SIZE + LP_CRC_SIZE];
        uint16_t len = build_packet(pkt, SC_NOENTRY, 0, 0, NULL, 0, NULL, 0);
        memcpy(&g_stream[w], pkt, len);
        w = (uint16_t)(w + len);
    }
    if (w < STREAM_CAP)
        memset(&g_stream[w], 0, (uint16_t)(STREAM_CAP - w));
    g_write = 0;
#endif

    // Start the circular DMA (polling mode — no transfer interrupt: the stream
    // never stops, so there is nothing to service). It re-reads the ring forever,
    // handing one byte to SPI1_TX per byte the master clocks.
    HAL_DMA_Start(&hdma_spi1_tx, (uint32_t)g_stream,
                  (uint32_t)&hspi1.Instance->DR, STREAM_CAP);
    SET_BIT(hspi1.Instance->CR2, SPI_CR2_TXDMAEN);
    __HAL_SPI_ENABLE(&hspi1);

#if LOGEMIT_DEBUG_SPI
    // Debug: also drain SPI1_RX so the master's MOSI bytes are captured (LOGEMIT_DEBUG_SPI).
    spi_rx_debug_init();
#endif

    // Configure the on-board activity LED (PC13) now that the DMA is running, so
    // the first read-index sample matches the live stream. See LogEmission_ActivityPoll.
    act_led_init();
}
