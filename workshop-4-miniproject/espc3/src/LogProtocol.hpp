#pragma once
#include <cstdint>
#include <cstddef>

// Wire protocol between this SPI master and every data-log slave.
//
// Delivery model: cumulative acknowledgement by record id.
// Every log record on a slave has a monotonically increasing 32-bit id. A
// CMD_GET_LOG request carries the highest id the master has already received and
// delivered ("lastReceivedId"). The slave treats that as an ack: it marks every
// record with id <= lastReceivedId as processed (and may free them) and replies
// with the oldest record whose id > lastReceivedId, or ST_NO_DATA if there is
// none. The master advances its high-water mark only after a frame passes
// validation, so a corrupted or lost response is simply re-requested at the same
// offset on the next pass — no record is lost, and processed records are never
// resent.
//
// Time model: master-as-reference.
// The master is the single time authority. It periodically pushes its own clock
// (ms on the master's timebase) to each slave via CMD_SET_TIME. A slave stamps
// every record with the event time on that timebase and, once it has been
// synced, sets FLAG_TIME_SYNCED on the record. Before the first sync (or after a
// slave reboot) it stamps its own uptime and leaves the flag clear, so a sink
// can tell absolute-ish master time from raw uptime. The master's clock is ms
// since boot today; swapping in SNTP/RTC later makes it true wall-clock with no
// protocol change. recordTime and lastReceivedId are u32 ms/id and wrap after
// ~49 days / 4 billion records respectively (fine for this project).
//
// Framing: SPI CS delimits every frame; presence and integrity are confirmed by
// MAGIC + checksum (an empty CS line reads 0xFF and can satisfy neither).
//
// Slave-side contract (see stm/miniproject-4 log_emission.c for a reference
// implementation):
//   * Keep a full 32-byte reply preloaded; each transaction sends the previous
//     reply and latches the new request. Parse the request on transfer-complete
//     and build the reply for the *next* transaction (the two-phase model).
//   * Validate the request checksum before acting; ignore a corrupt request so a
//     garbled lastReceivedId can never drop unprocessed records.
//   * Treat lastReceivedId as "mark <= this as processed"; only send id > it.
//   * Assign recordId monotonically; on buffer overflow it may skip ids (the
//     master follows the recordId it actually receives, so a gap is tolerated).
//   * Count every record discarded to overflow in `dropped` (saturating u16) and
//     report it in every response, so the master can flag lost data.
//   * On CMD_SET_TIME, adopt the supplied ms as its clock (set RTC or store an
//     offset) and stamp later records with FLAG_TIME_SYNCED set.
namespace logproto
{
    // First byte of a request frame (master -> slave).
    //
    // Two-phase exchange: a HAL SPI slave is still clocking in the command while
    // it must already be shifting out MISO, so it cannot answer in the same
    // frame. The master therefore sends the real command in one transaction
    // (ignoring MISO), then issues a CMD_NOP transaction to clock out the reply
    // the slave prepared. CMD_NOP mutates nothing on the slave.
    enum Command : uint8_t
    {
        CMD_NOP      = 0x00, // second-phase filler; read the prepared reply
        CMD_PING     = 0x01, // presence probe; args ignored
        CMD_GET_LOG  = 0x02, // ack up to lastReceivedId, return the next record
        CMD_SET_TIME = 0x03, // adopt masterTimeMs as the slave's clock
    };

    // First byte of every response frame (slave -> master).
    static constexpr uint8_t MAGIC = 0xA5;

    // Low bits of the status byte: what the response carries.
    enum Status : uint8_t
    {
        ST_OK      = 0x00, // payload/recordId/recordTime hold a valid record
        ST_NO_DATA = 0x01, // device alive, nothing newer than lastReceivedId
    };
    static constexpr uint8_t STATUS_CODE_MASK = 0x7F;
    // High bit of the status byte: the record's time is on the master timebase
    // (set), vs. raw slave uptime (clear).
    static constexpr uint8_t FLAG_TIME_SYNCED = 0x80;

    // Fixed 32-byte frame in both directions. Fields map 1:1 onto the wire via a
    // packed struct: this works only because both ends are little-endian (ESP32
    // and STM32 are) — do NOT port the slave to a big-endian part without adding
    // byte-swaps. Access multi-byte fields through the struct member (the
    // compiler emits safe unaligned loads); never take &field for an aligned
    // pointer. The static_asserts guard against padding creeping in.
    static constexpr int FRAME_SIZE  = 32;
    static constexpr int PAYLOAD_MAX  = 18;
    static constexpr int CHECKSUM_POS = FRAME_SIZE - 1; // 31

    // Request (master -> slave). arg = lastReceivedId (CMD_GET_LOG) or
    // masterTimeMs (CMD_SET_TIME); ignored for CMD_PING/CMD_NOP.
    struct __attribute__((packed)) Request
    {
        uint8_t  cmd;          // [0]
        uint32_t arg;          // [1..4]
        uint8_t  reserved[26]; // [5..30]
        uint8_t  checksum;     // [31]
    };

    // Response (slave -> master).
    struct __attribute__((packed)) Response
    {
        uint8_t  magic;              // [0]
        uint8_t  status;             // [1] code in low 7 bits, sync flag in bit 7
        uint8_t  len;                // [2] payload bytes used (0..PAYLOAD_MAX)
        uint32_t recordId;           // [3..6]
        uint32_t recordTimeMs;       // [7..10] master timebase, or uptime if !synced
        uint16_t dropped;            // [11..12] overflow-drop total (saturating)
        uint8_t  payload[PAYLOAD_MAX]; // [13..30]
        uint8_t  checksum;           // [31]
    };

    // A frame buffer: byte view for SPI/DMA + checksum, struct views for fields.
    union Frame
    {
        uint8_t  bytes[FRAME_SIZE];
        Request  req;
        Response resp;
    };

    static_assert(sizeof(Request)  == FRAME_SIZE, "Request layout drift (padding?)");
    static_assert(sizeof(Response) == FRAME_SIZE, "Response layout drift (padding?)");
    static_assert(sizeof(Frame)    == FRAME_SIZE, "Frame size drift");

    inline uint8_t checksum(const uint8_t* bytes)
    {
        uint8_t x = 0;
        for (int i = 0; i < CHECKSUM_POS; ++i)
            x ^= bytes[i];
        return x;
    }

    // Stamp the trailing checksum (call after filling a frame to send).
    inline void sign(Frame& f)
    {
        f.bytes[CHECKSUM_POS] = checksum(f.bytes);
    }

    // A response is genuine iff it starts with MAGIC and its checksum matches.
    inline bool valid(const Frame& f)
    {
        return f.resp.magic == MAGIC &&
               checksum(f.bytes) == f.bytes[CHECKSUM_POS];
    }

    inline uint8_t statusCode(const Response& r) { return r.status & STATUS_CODE_MASK; }
    inline bool    timeSynced(const Response& r) { return (r.status & FLAG_TIME_SYNCED) != 0; }
}
