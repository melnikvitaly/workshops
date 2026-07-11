#include "log_emission.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Protocol constants — mirror of espc3/src/LogProtocol.hpp. KEEP IN SYNC.
// ---------------------------------------------------------------------------
#define LP_FRAME_SIZE   32
#define LP_MAGIC        0xA5

// Commands (request byte 0). CMD_NOP is the master's second-phase filler.
#define CMD_NOP         0x00
#define CMD_PING        0x01
#define CMD_GET_LOG     0x02
#define CMD_SET_TIME    0x03

// Status (response byte 1): code in low 7 bits, sync flag in bit 7.
#define ST_OK           0x00
#define ST_NO_DATA      0x01
#define FLAG_TIME_SYNCED 0x80

#define LP_CHECKSUM_POS (LP_FRAME_SIZE - 1)

// ---------------------------------------------------------------------------
// Wire frame — packed-struct overlay, mirror of logproto::Frame on the master.
// Fields map 1:1 onto the wire because both ends are little-endian; do not port
// to a big-endian part without byte-swaps. Access multi-byte fields through the
// struct member (the compiler emits safe unaligned accesses).
// ---------------------------------------------------------------------------
typedef struct __attribute__((packed)) {
    uint8_t  cmd;          // [0]  lastReceivedId (GET_LOG) / masterTimeMs (SET_TIME)
    uint32_t arg;          // [1..4]
    uint8_t  reserved[26]; // [5..30]
    uint8_t  checksum;     // [31]
} LogRequest;

typedef struct __attribute__((packed)) {
    uint8_t  magic;                     // [0]
    uint8_t  status;                    // [1]
    uint8_t  len;                       // [2]
    uint32_t recordId;                  // [3..6]
    uint32_t recordTimeMs;              // [7..10]
    uint16_t dropped;                   // [11..12]
    uint8_t  payload[LOGEMIT_PAYLOAD_MAX]; // [13..30]
    uint8_t  checksum;                  // [31]
} LogResponse;

typedef union {
    uint8_t     bytes[LP_FRAME_SIZE];
    LogRequest  req;
    LogResponse resp;
} LogFrame;

_Static_assert(sizeof(LogRequest)  == LP_FRAME_SIZE, "LogRequest layout drift");
_Static_assert(sizeof(LogResponse) == LP_FRAME_SIZE, "LogResponse layout drift");
_Static_assert(sizeof(LogFrame)    == LP_FRAME_SIZE, "LogFrame size drift");

static uint8_t checksum(const uint8_t *f)
{
    uint8_t x = 0;
    for (int i = 0; i < LP_CHECKSUM_POS; ++i)
        x ^= f[i];
    return x;
}

// ---------------------------------------------------------------------------
// Record ring buffer. head/tail are monotonic counters; slot = idx % RING_CAP.
// tail is the oldest *unacknowledged* record; the master's cumulative ack moves
// it forward. Records are kept until acked, so a corrupted read simply retries.
// ---------------------------------------------------------------------------
#define RING_CAP 32

typedef struct {
    uint32_t id;
    uint32_t timeMs;
    uint8_t  synced;   // was the clock master-referenced when this was stamped?
    uint8_t  len;
    uint8_t  data[LOGEMIT_PAYLOAD_MAX];
} Record;

static Record            g_ring[RING_CAP];
static volatile uint32_t g_head    = 0;  // next write position (count)
static volatile uint32_t g_tail    = 0;  // oldest unacked position (count)
static volatile uint32_t g_nextId  = 1;  // id for the next queued record
static volatile uint16_t g_dropped = 0;  // records lost to overflow (saturating)

// ---------------------------------------------------------------------------
// Clock: master-as-reference. HAL_GetTick() is our local ms; SET_TIME gives an
// offset so nowMs() reads on the master timebase. Before the first sync we
// report raw uptime and leave FLAG_TIME_SYNCED clear.
// ---------------------------------------------------------------------------
static volatile int32_t g_timeOffsetMs = 0;
static volatile uint8_t g_timeSynced   = 0;

static uint32_t nowMs(void)
{
    return (uint32_t)((int32_t)HAL_GetTick() + g_timeOffsetMs);
}

// ---------------------------------------------------------------------------
// SPI slave state. Two-phase exchange with the master: a request received in
// one 32-byte transaction is answered in the *next* one. We therefore always
// keep a response preloaded in g_tx; on each completed transaction we parse the
// request that just arrived in g_rx, build the reply for the following
// transaction, and re-arm. g_tx starts as a valid idle frame so the very first
// read is well-formed.
// ---------------------------------------------------------------------------
static SPI_HandleTypeDef g_hspi;
static LogFrame          g_rx;
static LogFrame          g_tx;

// Build an idle response (valid frame, no record) for PING/SET_TIME/NOP.
static void build_idle(void)
{
    memset(&g_tx, 0, sizeof(g_tx));
    g_tx.resp.magic   = LP_MAGIC;
    g_tx.resp.status  = ST_NO_DATA | (g_timeSynced ? FLAG_TIME_SYNCED : 0);
    g_tx.resp.dropped = g_dropped;
    g_tx.bytes[LP_CHECKSUM_POS] = checksum(g_tx.bytes);
}

// Build the response to CMD_GET_LOG carrying lastReceivedId. First apply the
// cumulative ack (drop everything with id <= lastReceivedId), then return the
// oldest record that remains, or ST_NO_DATA.
static void build_get_log(uint32_t lastReceivedId)
{
    // Acknowledge: pop processed records off the tail.
    while (g_head != g_tail && g_ring[g_tail % RING_CAP].id <= lastReceivedId)
        g_tail++;

    memset(&g_tx, 0, sizeof(g_tx));
    g_tx.resp.magic = LP_MAGIC;

    uint8_t status = ST_NO_DATA;
    uint8_t synced = 0;
    if (g_head != g_tail)
    {
        const Record *r = &g_ring[g_tail % RING_CAP];
        status = ST_OK;
        synced = r->synced;   // the record's own stamp state, not the current one
        g_tx.resp.len          = r->len;
        g_tx.resp.recordId     = r->id;
        g_tx.resp.recordTimeMs = r->timeMs;
        memcpy(g_tx.resp.payload, r->data, r->len);
    }
    g_tx.resp.status  = status | (synced ? FLAG_TIME_SYNCED : 0);
    g_tx.resp.dropped = g_dropped;
    g_tx.bytes[LP_CHECKSUM_POS] = checksum(g_tx.bytes);
}

// Parse the request just received and prepare the reply for the next frame.
static void handle_request(void)
{
    // Requests are checksummed too; ignore a corrupt one so a garbled ack can
    // never drop unacked records. Leaving the previous g_tx would resend a stale
    // record, so fall back to idle.
    if (checksum(g_rx.bytes) != g_rx.bytes[LP_CHECKSUM_POS])
    {
        build_idle();
        return;
    }

    switch (g_rx.req.cmd)
    {
    case CMD_GET_LOG:
        build_get_log(g_rx.req.arg);
        break;
    case CMD_SET_TIME:
        g_timeOffsetMs = (int32_t)g_rx.req.arg - (int32_t)HAL_GetTick();
        g_timeSynced   = 1;
        build_idle();
        break;
    case CMD_PING:
    case CMD_NOP:
    default:
        build_idle();
        break;
    }
}

// ---------------------------------------------------------------------------
// Public API.
// ---------------------------------------------------------------------------
void LogEmission_Add(const uint8_t *data, uint8_t len)
{
    if (len > LOGEMIT_PAYLOAD_MAX)
        len = LOGEMIT_PAYLOAD_MAX;

    // The SPI ISR also moves g_tail (acking); mask it while we touch the ring.
    HAL_NVIC_DisableIRQ(SPI1_IRQn);

    if (g_head - g_tail >= RING_CAP)
    {
        g_tail++;                       // ring full: drop oldest (id gap)
        if (g_dropped != 0xFFFF)
            g_dropped++;                // report the loss to the master
    }

    Record *r = &g_ring[g_head % RING_CAP];
    r->id     = g_nextId++;
    r->timeMs = nowMs();
    r->synced = g_timeSynced;
    r->len    = len;
    memcpy(r->data, data, len);
    g_head++;

    HAL_NVIC_EnableIRQ(SPI1_IRQn);
}

void LogEmission_AddText(const char *text)
{
    size_t n = strlen(text);
    if (n > LOGEMIT_PAYLOAD_MAX)
        n = LOGEMIT_PAYLOAD_MAX;
    LogEmission_Add((const uint8_t *)text, (uint8_t)n);
}

void LogEmission_Init(void)
{
    g_hspi.Instance               = SPI1;
    g_hspi.Init.Mode              = SPI_MODE_SLAVE;
    g_hspi.Init.Direction         = SPI_DIRECTION_2LINES;
    g_hspi.Init.DataSize          = SPI_DATASIZE_8BIT;
    g_hspi.Init.CLKPolarity       = SPI_POLARITY_LOW;   // mode 0
    g_hspi.Init.CLKPhase          = SPI_PHASE_1EDGE;    // mode 0
    g_hspi.Init.NSS               = SPI_NSS_HARD_INPUT;  // CS frames each xfer
    g_hspi.Init.FirstBit          = SPI_FIRSTBIT_MSB;
    g_hspi.Init.TIMode            = SPI_TIMODE_DISABLE;
    g_hspi.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    g_hspi.Init.CRCPolynomial     = 10;
    // BaudRatePrescaler is ignored in slave mode (the master clocks the bus).
    g_hspi.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
    if (HAL_SPI_Init(&g_hspi) != HAL_OK)
        return;

    build_idle();  // valid frame ready before the first request arrives
    HAL_SPI_TransmitReceive_IT(&g_hspi, g_tx.bytes, g_rx.bytes, LP_FRAME_SIZE);
}

// ---------------------------------------------------------------------------
// HAL glue (weak overrides). Everything SPI1 lives here so main.c stays clean.
// ---------------------------------------------------------------------------
void HAL_SPI_MspInit(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != SPI1)
        return;

    __HAL_RCC_SPI1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF5_SPI1;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_NVIC_SetPriority(SPI1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(SPI1_IRQn);
}

void SPI1_IRQHandler(void)
{
    HAL_SPI_IRQHandler(&g_hspi);
}

void HAL_SPI_TxRxCpltCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != SPI1)
        return;
    handle_request();
    HAL_SPI_TransmitReceive_IT(&g_hspi, g_tx.bytes, g_rx.bytes, LP_FRAME_SIZE);
}

void HAL_SPI_ErrorCallback(SPI_HandleTypeDef *hspi)
{
    if (hspi->Instance != SPI1)
        return;
    // A CS glitch / short clock can abort a transfer; reset framing to a known
    // idle frame and re-arm so the next CS assertion starts clean.
    __HAL_SPI_CLEAR_OVRFLAG(&g_hspi);
    build_idle();
    HAL_SPI_TransmitReceive_IT(&g_hspi, g_tx.bytes, g_rx.bytes, LP_FRAME_SIZE);
}
