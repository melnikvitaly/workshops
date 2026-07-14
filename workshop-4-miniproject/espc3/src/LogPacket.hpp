#pragma once
#include <cstdint>
#include <cstddef>

// Wire data structures for the streaming log protocol — the on-the-wire layout
// only (constants, the value-type enum, the packet header, and trivial layout
// helpers). The protocol behaviour (CRC, packet parsing, the delivery/presence
// model) lives in LogProtocol.hpp, which includes this file.
//
// Every packet is: [HEADER_SIZE-byte fixed header][payload 0..255][crc16].
// Multi-byte fields are little-endian; both MCUs are little-endian, so the
// packed structs map 1:1 onto the bytes. The STM slave mirrors these constants
// in log_emission.c — keep the two in sync.
namespace logproto
{
    // First byte of every packet; also the resync marker.
    static constexpr uint8_t MAGIC = 0xA5;

    // Status byte: low nibble = what the packet is, high bits = flags.
    static constexpr uint8_t SC_ENTRY     = 0x00; // carries a DataEntry
    static constexpr uint8_t SC_NOENTRY   = 0x01; // idle/heartbeat, no entry
    static constexpr uint8_t SC_CODE_MASK = 0x0F;
    static constexpr uint8_t FLAG_OVERFLOW = 0x80; // slave has dropped records
    static constexpr uint8_t FLAG_NEARFULL = 0x40; // ring filling faster than drained

    // Type of the value carried in `payload` (payloadLen bytes, little-endian
    // for the numeric types; raw chars for VT_STR; a DateTime for VT_DATETIME).
    enum ValueType : uint8_t
    {
        VT_U8, VT_U16, VT_U32,
        VT_I8, VT_I16, VT_I32,
        VT_F32,
        VT_STR,
        VT_DATETIME,   // payload is a 6-byte DateTime (e.g. an RTC reading)
    };

    static constexpr int OBJID_LEN    = 4;
    static constexpr int HEADER_SIZE  = 18;   // magic..payloadLen
    static constexpr int CRC_SIZE     = 2;
    static constexpr int PAYLOAD_MAX  = 255;  // payloadLen is a u8
    static constexpr int MIN_PACKET   = HEADER_SIZE + 0 + CRC_SIZE;           // 20
    static constexpr int MAX_PACKET   = HEADER_SIZE + PAYLOAD_MAX + CRC_SIZE; // 275

    // A broken-down date/time, used as a VT_DATETIME value payload (e.g. an RTC
    // reading logged as a sensor). Binary (not BCD); year is the 2000-based
    // offset (e.g. 25 = 2025). Not a header field.
    struct __attribute__((packed)) DateTime
    {
        uint8_t year;    // 0..99  (2000 + year)
        uint8_t month;   // 1..12
        uint8_t date;    // 1..31
        uint8_t hours;   // 0..23
        uint8_t minutes; // 0..59
        uint8_t seconds; // 0..59
    };
    static_assert(sizeof(DateTime) == 6, "DateTime layout drift (padding?)");

    // Fixed part of every packet. The variable payload and the 2-byte CRC follow
    // it on the wire (they are not struct members because payloadLen varies).
    struct __attribute__((packed)) Header
    {
        uint8_t  magic;                 // [0]
        uint8_t  status;                // [1]     code + flags
        uint32_t seq;                   // [2..5]   monotonic per ENTRY (dedup/gap)
        uint16_t dropped;               // [6..7]   overflow count (saturating)
        uint32_t timestamp;             // [8..11]  slave uptime ms when queued
        char     objectId[OBJID_LEN];   // [12..15] fixed 4-char id
        uint8_t  valueType;             // [16]     ValueType
        uint8_t  payloadLen;            // [17]     payload bytes that follow (0..255)
    };
    static_assert(sizeof(Header) == HEADER_SIZE, "Header layout drift (padding?)");

    inline uint8_t statusCode(const Header& h) { return h.status & SC_CODE_MASK; }
    inline bool    isEntry(const Header& h)    { return statusCode(h) == SC_ENTRY; }
    inline bool    overflow(const Header& h)   { return (h.status & FLAG_OVERFLOW) != 0; }
    inline bool    nearFull(const Header& h)   { return (h.status & FLAG_NEARFULL) != 0; }

    // Total on-wire length of a packet with the given payload length.
    inline size_t packetLen(uint8_t payloadLen)
    {
        return HEADER_SIZE + payloadLen + CRC_SIZE;
    }
}
