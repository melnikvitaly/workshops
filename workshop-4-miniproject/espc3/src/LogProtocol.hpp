#pragma once
#include "LogPacket.hpp"   // wire data structures (Header, DateTime, constants)
#include <cstdint>
#include <cstddef>

// One-way streaming log protocol: every slave continuously presents a byte
// stream of self-framed packets on MISO, and this master clocks blocks of it out
// with DMA and parses the packets. There is no request, no acknowledgement, no
// time sync and no command set — the master only ever clocks dummy bytes. This
// is deliberately best-effort: a record the master fails to collect before the
// slave's ring overwrites it is simply lost, and the slave reports how much it
// has dropped so the master can flag it.
//
// The wire layout (Header, DateTime, ValueType, constants) is in LogPacket.hpp;
// this file adds the behaviour: the CRC and the stream parser.
//
// Delivery model: best-effort stream.
//   The slave keeps a circular byte buffer, pre-filled with NoEntry packets, and
//   feeds it to MISO with free-running circular DMA. A new record is written into
//   the ring as a fresh packet, overwriting the oldest bytes. The master reads
//   forward through this stream; it never rewinds, so within one lap of the ring
//   it sees each packet at most once. If the master clocks faster than the slave
//   produces, the ring's read pointer laps around and re-presents old packets —
//   the master de-duplicates ENTRY packets by their monotonic `seq` (only a seq
//   strictly greater than the last delivered one is dispatched). A slave that
//   produces faster than the master drains overwrites not-yet-read bytes; it
//   counts those overflow events in `dropped`, reported in every packet.
//
// Presence: inferred from a valid framed packet.
//   SPI has no ACK, and an unselected/empty CS line reads 0xFF (MISO is held high
//   by an internal pull-up, see SpiBus::init) which can never satisfy MAGIC + a
//   correct CRC. A slave that has nothing to log therefore still presents valid
//   NoEntry packets (from the ring pre-fill), so "a valid packet appeared this
//   pass" is a reliable present/absent test.
//
// Time: slaves stamp their own uptime.
//   There is no master clock push; `timestamp` is the slave's HAL_GetTick() ms at
//   the moment the record was queued (for ordering/latency). Wall-clock is not a
//   header field — the RTC is treated as just another sensor: a slave logs it as
//   an ordinary entry with valueType VT_DATETIME (a 6-byte payload). The master
//   additionally records its own collection time (collectedAtUs) per record.
//
// Framing: self-synchronising, so the master can lock onto the stream at any
//   offset. The header ends at payloadLen so the length is known before the
//   (variable) tail; the CRC covers header+payload. A corrupt/torn packet (e.g.
//   at the seam where a new record partially overwrote an old one) fails the CRC
//   and the parser resynchronises to the next MAGIC.
namespace logproto
{
    // CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF). Table-less; the slave uses
    // the identical routine. Covers header+payload (everything before the CRC).
    inline uint16_t crc16(const uint8_t* d, size_t n)
    {
        uint16_t crc = 0xFFFF;
        for (size_t i = 0; i < n; ++i)
        {
            crc ^= (uint16_t)d[i] << 8;
            for (int b = 0; b < 8; ++b)
                crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                     : (uint16_t)(crc << 1);
        }
        return crc;
    }

    enum ParseResult
    {
        PKT_OK,        // a full, CRC-valid packet starts at buf[0]
        PKT_NEED_MORE, // buf[0] may start a packet but not all bytes are present
        PKT_BAD,       // buf[0] is not a valid packet start; skip `consumed` bytes
    };

    // Try to lock one packet at the front of `buf` (`avail` bytes readable).
    //  * PKT_OK       -> `consumed` = full packet length; header/payload valid.
    //  * PKT_NEED_MORE-> caller should read more bytes and retry (nothing consumed).
    //  * PKT_BAD      -> `consumed` = 1; caller advances one byte to resync on the
    //                    next MAGIC (covers seams, 0xFF idle lines, and torn frames
    //                    whose CRC fails).
    // Transport-agnostic: `buf` is any byte window of the stream.
    inline ParseResult parsePacket(const uint8_t* buf, size_t avail, size_t& consumed)
    {
        consumed = 0;
        if (avail < 1)
            return PKT_NEED_MORE;
        if (buf[0] != MAGIC)
        {
            consumed = 1;
            return PKT_BAD;
        }
        if (avail < HEADER_SIZE)
            return PKT_NEED_MORE;

        const uint8_t payloadLen = buf[HEADER_SIZE - 1];
        const size_t  total      = packetLen(payloadLen);
        if (avail < total)
            return PKT_NEED_MORE;

        const size_t   crcPos = HEADER_SIZE + payloadLen;
        const uint16_t got    = (uint16_t)buf[crcPos] | ((uint16_t)buf[crcPos + 1] << 8);
        if (got != crc16(buf, crcPos))
        {
            consumed = 1;           // false MAGIC or torn packet: resync
            return PKT_BAD;
        }
        consumed = total;
        return PKT_OK;
    }
}
