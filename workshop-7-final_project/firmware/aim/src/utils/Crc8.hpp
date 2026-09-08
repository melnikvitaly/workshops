#pragma once
#include <cstdint>
#include <cstddef>

// CRC-8/ATM (a.k.a. CRC-8/SMBUS): poly 0x07, init 0x00, no reflection, no final
// XOR. Guards NDJSON lines on the EYE link - see docs/protocol.md §3.2.
//
// Conformance vectors (asserted by the caller's unit check and by EYE):
//   crc8("123456789")                          == 0xF4
//   crc8("{\"t\":\"cfg.get\",\"k\":\"input.channel\"}") == 0x8A
namespace crc8
{
    inline uint8_t compute(const uint8_t *data, size_t len)
    {
        uint8_t crc = 0x00;
        for (size_t i = 0; i < len; ++i)
        {
            crc ^= data[i];
            for (uint8_t bit = 0; bit < 8; ++bit)
                crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x07) : (uint8_t)(crc << 1);
        }
        return crc;
    }

    inline uint8_t compute(const char *data, size_t len)
    {
        return compute(reinterpret_cast<const uint8_t *>(data), len);
    }
}
