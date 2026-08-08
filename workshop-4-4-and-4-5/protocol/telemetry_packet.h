#ifndef TELEMETRY_PACKET_H
#define TELEMETRY_PACKET_H

/* ============================================================================
 *  Custom SPI telemetry protocol — THE wire format, shared by both firmwares
 * ============================================================================
 *  This one file is compiled by both ends, so the layout physically cannot
 *  drift:
 *      STM32 master  -> stm/workshop-4-4  (include path "../../../protocol")
 *      ESP32-C3 slave-> espc3             (INCLUDE_DIRS "../../protocol")
 *  It is plain C89-compatible C, valid as-is in C++ — no namespace, no HAL, no
 *  ESP-IDF, nothing but <stdint.h>. (The previous workshop kept two hand-mirrored
 *  copies of its header and the "keep them in sync" comment was the whole
 *  maintenance story; one file removes that class of bug.)
 *
 *  Link model — synchronous, master-paced, one frame per CS assertion
 *  --------------------------------------------------------------------------
 *  The STM32 is the SPI master and the only talker: once a second it lowers CS,
 *  clocks out exactly TELEMETRY_FRAME_SIZE bytes with a blocking transmit, and
 *  raises CS. The ESP32-C3 slave has a receive transaction of exactly that size
 *  armed, so one CS assertion = one whole frame = one slave transaction. There
 *  is no request/response, no DMA streaming, no ring buffer and no resync
 *  scanner: framing is the CS line itself.
 *
 *  Why fixed size: an ESP32 SPI slave transaction is armed with a length before
 *  the master starts clocking, so a variable-length frame would need either a
 *  worst-case-sized transaction (and a length field the hardware ignores) or a
 *  two-transaction header/body handshake. Every value here is known at compile
 *  time, so a fixed 32-byte frame is both simpler and enough.
 *
 *  Byte order is little-endian on the wire; both MCUs are little-endian, so the
 *  packed structs below map 1:1 onto the bytes and no swapping is needed.
 *
 *  Integrity: magic0/magic1/version reject a mis-clocked or foreign frame
 *  cheaply, and CRC-16/CCITT-FALSE over everything before it catches bit slip.
 *  A slave that is not armed when the master transmits simply misses that
 *  second's frame — the next one arrives 1 s later, so there is no recovery
 *  logic beyond "validate and drop".
 * ============================================================================ */

#include <stdint.h>

/* --- Framing --------------------------------------------------------------- */
#define TELEMETRY_MAGIC0    0xA5u  /* first byte of every frame  */
#define TELEMETRY_MAGIC1    0x5Au  /* second byte: 0xA5 inverted */
#define TELEMETRY_VERSION   0x01u  /* bump on any layout change  */

/* --- Validity flags -------------------------------------------------------- */
/* A sensor that failed to answer this cycle clears its bit and leaves the
 * corresponding fields at their last known value; the receiver prints "--"
 * rather than a stale number pretending to be fresh. */
#define TELEMETRY_FLAG_TIME_VALID   0x01u  /* date/time fields read from the RTC  */
#define TELEMETRY_FLAG_ENV_VALID    0x02u  /* temperature/humidity/pressure fresh */
#define TELEMETRY_FLAG_LIGHT_VALID  0x04u  /* light percent/raw fresh             */

/* --- Fixed-point scaling ---------------------------------------------------
 * Floats are avoided on the wire: the STM32F401 has no FPU enabled here and a
 * scaled integer is exact, endian-trivial and half the width of a double. The
 * receiver divides by these to print. */
#define TELEMETRY_TEMP_SCALE      100  /* tempC100      -> °C   */
#define TELEMETRY_HUMIDITY_SCALE  100  /* humidity100   -> %RH  */
#define TELEMETRY_PRESSURE_PER_HPA 100 /* pressurePa    -> hPa  */

/* --- Field ranges (for the receiver's sanity check) ------------------------ */
#define TELEMETRY_PERCENT_MAX     100u
#define TELEMETRY_LIGHT_RAW_MAX   4095u   /* 12-bit ADC full scale */

/* Payload: everything the workshop asks the link to carry — date, time,
 * temperature, light level percentage, humidity and pressure — plus the
 * bookkeeping needed to tell a fresh frame from a repeat. */
typedef struct __attribute__((packed))
{
    /* Date and time, straight from the DS1307 (binary, not BCD). */
    uint8_t  year;         /* 0..99, 2000-based (25 = 2025) */
    uint8_t  month;        /* 1..12 */
    uint8_t  day;          /* 1..31 */
    uint8_t  hour;         /* 0..23 (24-hour) */
    uint8_t  minute;       /* 0..59 */
    uint8_t  second;       /* 0..59 */

    uint8_t  lightPct;     /* light level, 0..100 % of ADC full scale */
    uint8_t  flags;        /* TELEMETRY_FLAG_* — which fields are fresh */

    int16_t  tempC100;     /* temperature, °C * 100 (signed: BME280 reads below 0) */
    uint16_t humidity100;  /* relative humidity, %RH * 100 (0..10000) */
    uint32_t pressurePa;   /* pressure in pascal (divide by 100 for hPa) */

    uint16_t lightRaw;     /* raw 12-bit ADC average, 0..4095 (percent loses ~41 counts/step) */
    uint32_t uptimeMs;     /* master's HAL_GetTick() when the frame was built */
    uint32_t seq;          /* monotonic frame counter — gaps = frames the slave missed */
} TelemetryPayload;

#define TELEMETRY_PAYLOAD_SIZE 26

typedef struct __attribute__((packed))
{
    uint8_t          magic0;      /* TELEMETRY_MAGIC0  */
    uint8_t          magic1;      /* TELEMETRY_MAGIC1  */
    uint8_t          version;     /* TELEMETRY_VERSION */
    uint8_t          payloadLen;  /* TELEMETRY_PAYLOAD_SIZE, checked by the receiver */
    TelemetryPayload payload;
    uint16_t         crc;         /* CRC-16/CCITT-FALSE over the 30 bytes before it */
} TelemetryFrame;

#define TELEMETRY_HEADER_SIZE 4
#define TELEMETRY_CRC_SIZE    2
#define TELEMETRY_FRAME_SIZE  (TELEMETRY_HEADER_SIZE + TELEMETRY_PAYLOAD_SIZE + TELEMETRY_CRC_SIZE)  /* 32 */

/* Layout is the contract; a compiler that pads either struct breaks the link
 * silently, so fail the build instead. */
#if defined(__cplusplus)
static_assert(sizeof(TelemetryPayload) == TELEMETRY_PAYLOAD_SIZE, "TelemetryPayload padded");
static_assert(sizeof(TelemetryFrame) == TELEMETRY_FRAME_SIZE, "TelemetryFrame padded");
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(TelemetryPayload) == TELEMETRY_PAYLOAD_SIZE, "TelemetryPayload padded");
_Static_assert(sizeof(TelemetryFrame) == TELEMETRY_FRAME_SIZE, "TelemetryFrame padded");
#endif

/* --- CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) -------------------------
 * Table-less: 32 bytes a second on one side and one frame per second on the
 * other make the 256-entry table pure waste. Both ends call this same code, so
 * "the CRCs disagree" can only ever mean the bytes disagree. */
#define TELEMETRY_CRC_INIT  0xFFFFu
#define TELEMETRY_CRC_POLY  0x1021u
#define TELEMETRY_CRC_MSB   0x8000u
#define TELEMETRY_CRC_BITS  8

static inline uint16_t Telemetry_Crc16(const uint8_t *data, uint16_t len)
{
    uint16_t crc = TELEMETRY_CRC_INIT;
    uint16_t i;
    for (i = 0; i < len; ++i)
    {
        int b;
        crc ^= (uint16_t)((uint16_t)data[i] << TELEMETRY_CRC_BITS);
        for (b = 0; b < TELEMETRY_CRC_BITS; ++b)
        {
            crc = (crc & TELEMETRY_CRC_MSB)
                      ? (uint16_t)((uint16_t)(crc << 1) ^ TELEMETRY_CRC_POLY)
                      : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* Bytes the CRC covers: the header and the payload, i.e. all but the CRC itself. */
#define TELEMETRY_CRC_COVERAGE (TELEMETRY_HEADER_SIZE + TELEMETRY_PAYLOAD_SIZE)

/* Fill in the constant header fields and stamp the CRC. The caller fills
 * `frame->payload` first; this finishes the frame so it is ready to clock out. */
static inline void Telemetry_Seal(TelemetryFrame *frame)
{
    frame->magic0     = TELEMETRY_MAGIC0;
    frame->magic1     = TELEMETRY_MAGIC1;
    frame->version    = TELEMETRY_VERSION;
    frame->payloadLen = TELEMETRY_PAYLOAD_SIZE;
    frame->crc        = Telemetry_Crc16((const uint8_t *)frame, TELEMETRY_CRC_COVERAGE);
}

/* Why a frame was rejected — the receiver prints this, so a wiring or version
 * problem is distinguishable from line noise instead of all looking like "no
 * data". */
typedef enum
{
    TELEMETRY_OK = 0,
    TELEMETRY_ERR_MAGIC,    /* not a frame start: CS/clock misalignment, or noise */
    TELEMETRY_ERR_VERSION,  /* the two firmwares were built from different layouts */
    TELEMETRY_ERR_LENGTH,   /* payloadLen disagrees with this build's struct */
    TELEMETRY_ERR_CRC,      /* framing was right but bits slipped on the wire */
} TelemetryStatus;

/* Validate TELEMETRY_FRAME_SIZE bytes received on the wire. `raw` may be any
 * byte buffer (it is copied into *out only on success, so a rejected frame can
 * never leave half-parsed values behind). */
static inline TelemetryStatus Telemetry_Parse(const uint8_t *raw, TelemetryPayload *out)
{
    TelemetryFrame frame;
    uint16_t       crc;
    const uint8_t *bytes = (const uint8_t *)&frame;

    /* memcpy by hand: no <string.h> so the header stays dependency-free, and 32
     * bytes once a second is not worth a library call. */
    {
        uint16_t i;
        uint8_t *dst = (uint8_t *)&frame;
        for (i = 0; i < TELEMETRY_FRAME_SIZE; ++i)
            dst[i] = raw[i];
    }

    if (frame.magic0 != TELEMETRY_MAGIC0 || frame.magic1 != TELEMETRY_MAGIC1)
        return TELEMETRY_ERR_MAGIC;
    if (frame.version != TELEMETRY_VERSION)
        return TELEMETRY_ERR_VERSION;
    if (frame.payloadLen != TELEMETRY_PAYLOAD_SIZE)
        return TELEMETRY_ERR_LENGTH;

    crc = Telemetry_Crc16(bytes, TELEMETRY_CRC_COVERAGE);
    if (crc != frame.crc)
        return TELEMETRY_ERR_CRC;

    *out = frame.payload;
    return TELEMETRY_OK;
}

static inline const char *Telemetry_StatusName(TelemetryStatus s)
{
    switch (s)
    {
        case TELEMETRY_OK:          return "ok";
        case TELEMETRY_ERR_MAGIC:   return "bad magic";
        case TELEMETRY_ERR_VERSION: return "version mismatch";
        case TELEMETRY_ERR_LENGTH:  return "bad payload length";
        case TELEMETRY_ERR_CRC:     return "crc mismatch";
        default:                    return "unknown";
    }
}

#endif /* TELEMETRY_PACKET_H */
