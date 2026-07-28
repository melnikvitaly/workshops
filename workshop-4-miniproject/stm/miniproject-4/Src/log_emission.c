#include "log_emission.h"
#include "main.h"   // Error_Handler()
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

// Monotonic (never-wraps-at-STREAM_CAP) count of bytes ever placed into the
// ring, producer side. Starts at STREAM_CAP right after the initial pre-fill
// (see LogEmission_Init) so it and stream_read_total() below share the same
// zero point: "the whole ring holds valid content as of g_write==0". Paired
// with stream_read_total() to detect real overflow — see the long comment on
// stream_write() for why a lap-relative comparison (what this replaced) gives
// false positives here.
static volatile uint32_t g_writeTotal = 0;

// SPI1 and its TX DMA are configured entirely by CubeMX: MX_SPI1_Init (slave,
// mode 0, hardware NSS, PA4..PA7) and MX_DMA_Init + HAL_SPI_MspInit set up the
// circular DMA2 Stream3 handle `hdma_spi1_tx` and link it to hspi1. This module
// just starts that circular DMA on the ring and drives the stream.
extern SPI_HandleTypeDef hspi1;
extern DMA_HandleTypeDef hdma_spi1_tx;

// Current DMA read index (bytes already handed to the SPI shift path this lap).
static uint16_t stream_read_index(void)
{
    uint16_t ndtr = (uint16_t)__HAL_DMA_GET_COUNTER(&hdma_spi1_tx);
    return (uint16_t)(STREAM_CAP - ndtr);
}

// ---------------------------------------------------------------------------
// Throughput counters (packets queued, bytes clocked out) for the OLED.
//
// The read index alone cannot measure throughput: it wraps every STREAM_CAP
// bytes, which at 1 MHz is ~16 ms, and the main loop can be away far longer than
// that (one SSD1306 flush is ~1 KB over 100 kHz I2C ≈ 100 ms, i.e. several laps).
// A modular delta would silently drop whole laps. So the TX DMA's
// transfer-complete interrupt counts laps instead, and the byte total is just
// laps * STREAM_CAP. That is the only reason this DMA runs with an interrupt at
// all; the stream itself still needs no servicing.
// ---------------------------------------------------------------------------
static volatile uint32_t g_pktTotal = 0;  // ENTRY packets queued since boot
static volatile uint32_t g_dmaLaps  = 0;  // full ring laps completed (ISR-owned)

static uint32_t g_windowMs      = 0;      // start of the current 1 s rate window
static uint32_t g_pktAtWindow   = 0;
static uint32_t g_bytesAtWindow = 0;
static uint32_t g_pktRate       = 0;      // packets/s over the last full window
static uint32_t g_byteRate      = 0;      // bytes/s over the last full window

// DMA2 Stream3 transfer-complete: one more lap of the ring has been clocked out.
// Circular mode, so the stream keeps running; this only bumps the counter.
static void stream_tx_lap_cb(DMA_HandleTypeDef *hdma)
{
    (void)hdma;
    ++g_dmaLaps;
}

// Bytes handed to SPI1 since boot, counted in whole ring laps. Deliberately
// ignores the partial lap in progress: mixing the lap count with the live read
// index means the two can disagree by a whole ring if a lap lands between the two
// reads, which needs a retry loop to paper over. Counting laps alone is a single
// atomic read of an aligned 32-bit word, and the cost is only granularity —
// STREAM_CAP bytes, i.e. the rate below is quantised to ~2 KB/s (±1.6 % at the
// ~125 KB/s this link runs at). The error does not accumulate: laps are exact.
static uint32_t stream_bytes_total(void)
{
    return g_dmaLaps * (uint32_t)STREAM_CAP;
}

// Monotonic (never-wraps-at-STREAM_CAP) count of bytes the TX DMA has handed
// to the SPI shift path since boot: g_dmaLaps (ISR-owned) times STREAM_CAP,
// plus the live in-lap position. Unlike stream_bytes_total() above, this
// *does* combine the lap count with the live read index — needed here for an
// unambiguous ahead/behind comparison against g_writeTotal (see
// stream_write()) — so it pays for that with the retry loop
// stream_bytes_total()'s own comment says to avoid: read the lap count,
// sample the index, then re-check the lap count didn't change out from under
// us (a lap boundary landing mid-read would otherwise pair a stale lap count
// with a post-wrap index, undercounting by a whole ring).
static uint32_t stream_read_total(void)
{
    uint32_t laps;
    uint16_t pos;
    do
    {
        laps = g_dmaLaps;
        pos  = stream_read_index();
    } while (laps != g_dmaLaps);
    return laps * (uint32_t)STREAM_CAP + pos;
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
    if (HAL_DMA_Init(&hdma_spi1_rx) != HAL_OK)
    {
        Error_Handler();
    }
    if (HAL_DMA_Start(&hdma_spi1_rx, (uint32_t)&hspi1.Instance->DR,
                      (uint32_t)g_spiRxDebug, SPI_RX_DEBUG_LEN) != HAL_OK)
    {
        Error_Handler();
    }
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
//
// Overflow detection uses monotonic (never-wrapping) totals, not a lap-
// relative position comparison, and that distinction matters here. This
// stream's producer (this function, paced by TIM2/ADC — see LogEmission_Init)
// is far slower than its consumer: the master polls continuously, so one TX
// DMA lap (STREAM_CAP bytes) completes in ~16 ms at 1 MHz while records land
// every ~40-80 ms. That means stream_read_index() (lap-relative, resets to 0
// every lap) sweeps past g_write's position several times between any two
// producer writes, making its value effectively uncorrelated with g_write at
// write-time. A naive "(g_write - r) mod STREAM_CAP" can't tell "producer is
// genuinely near a full ring ahead of the reader" from "the reader raced past
// the producer's position and is most of a lap ahead of it" — both look like
// a large forward distance from r to g_write under modular arithmetic, so the
// old version flagged a false overflow roughly packetSize/STREAM_CAP of the
// time even under perfectly healthy, fully-drained operation. Comparing
// monotonic totals instead removes the ambiguity: g_writeTotal only grows on
// real writes, stream_read_total() only grows as bytes are genuinely clocked
// out, and the signed subtraction below is well-defined regardless of how
// many laps either side has completed.
static void stream_write(const uint8_t *p, uint16_t n)
{
    if (n == 0 || n > STREAM_CAP)
        return;

    int32_t  rawBacklog = (int32_t)(g_writeTotal - stream_read_total());
    uint32_t backlog     = rawBacklog > 0 ? (uint32_t)rawBacklog : 0;
    if (backlog + n >= STREAM_CAP && g_dropped != 0xFFFF)
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
    g_write       = (uint16_t)((g_write + n) % STREAM_CAP);
    g_writeTotal += n;
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
    ++g_pktTotal;
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

    uint16_t r   = stream_read_index();
    uint32_t now = HAL_GetTick();

    if (r != g_lastReadIdx)             // master clocked bytes since the last poll
    {
        g_lastReadIdx = r;
        g_lastActMs   = now;
        if (!g_ledOn) { ACT_LED_ON(); g_ledOn = 1; }
    }
    else if (g_ledOn && (now - g_lastActMs) >= ACT_BLINK_MS)
    {
        ACT_LED_OFF();                  // no traffic for ACT_BLINK_MS -> release
        g_ledOn = 0;
    }

    // Close the rate window once a second. dt is measured rather than assumed to
    // be 1000 ms: the main loop can overshoot (a blocking OLED flush), and
    // dividing by the real elapsed time keeps the rate honest.
    if (g_windowMs == 0)
    {
        g_windowMs      = now;
        g_pktAtWindow   = g_pktTotal;
        g_bytesAtWindow = stream_bytes_total();
    }
    else if ((uint32_t)(now - g_windowMs) >= 1000u)
    {
        uint32_t dtMs  = now - g_windowMs;
        uint32_t pkt   = g_pktTotal;
        uint32_t bytes = stream_bytes_total();

        g_pktRate  = ((pkt - g_pktAtWindow) * 1000u) / dtMs;
        // 64-bit intermediate: the byte delta can reach ~125 000 per second, and
        // after a long stall (delta * 1000) would overflow 32 bits.
        g_byteRate = (uint32_t)(((uint64_t)(bytes - g_bytesAtWindow) * 1000u) / dtMs);

        g_windowMs      = now;
        g_pktAtWindow   = pkt;
        g_bytesAtWindow = bytes;
    }
}

void LogEmission_GetStats(LogEmitStats *out)
{
    if (!out)
        return;
    out->packetsPerSec = g_pktRate;
    out->bytesPerSec   = g_byteRate;
    out->totalPackets  = g_pktTotal;
    out->totalBytes    = stream_bytes_total();
    out->dropped       = g_dropped;
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

    // The whole ring now holds valid initial content, i.e. conceptually
    // "written" once through — give g_writeTotal the matching starting value
    // so it and stream_read_total() (which starts at 0 as DMA begins reading
    // from position 0 below) share the same zero point. See stream_write().
    g_writeTotal = STREAM_CAP;

    // Start the circular DMA. It re-reads the ring forever, handing one byte to
    // SPI1_TX per byte the master clocks — the stream never stops and needs no
    // servicing. The transfer-complete interrupt is enabled purely to count laps
    // for the throughput stats (see stream_bytes_total); DMA2_Stream3_IRQn is
    // already NVIC-enabled by MX_DMA_Init and dispatched in stm32f4xx_it.c.
    hdma_spi1_tx.XferCpltCallback     = stream_tx_lap_cb;
    hdma_spi1_tx.XferHalfCpltCallback = NULL;  // half-transfer IRQ stays off
    hdma_spi1_tx.XferErrorCallback    = NULL;
    if (HAL_DMA_Start_IT(&hdma_spi1_tx, (uint32_t)g_stream,
                         (uint32_t)&hspi1.Instance->DR, STREAM_CAP) != HAL_OK)
    {
        Error_Handler();  // no TX DMA means no SPI stream at all: unrecoverable
    }
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
