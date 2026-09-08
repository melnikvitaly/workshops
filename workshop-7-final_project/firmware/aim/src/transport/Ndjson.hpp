#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cstdio>
#include "Crc8.hpp"

// Minimal NDJSON for the config plane (docs/protocol.md §3). Enough to carry
// cfg.set / cfg.get / cfg.reset / estop in, and cfg.state / evt / tlm out, with
// the mandatory CRC-8 suffix. The 256-byte cap discard-and-count path and a
// hardened tokeniser are task #3; this assumes well-formed lines from EYE and
// leaves anything else to be counted as `unparsed` by the caller.
//
// Objects are flat, so field lookup is a substring scan for "key" followed by
// ':' and a value. Good enough for the fixed set of messages above.
namespace ndjson
{
    // Validate the *XX suffix. On success points `json`/`jsonLen` at the object
    // bytes, '{' through '}' inclusive (what the CRC covers).
    inline bool checkLine(const char *line, const char **json, size_t *jsonLen)
    {
        if (!line || line[0] != '{')
            return false;

        const char *close = std::strrchr(line, '}');
        if (!close || close[1] != '*')
            return false;
        if (!std::isxdigit((unsigned char)close[2]) || !std::isxdigit((unsigned char)close[3]) ||
            close[4] != '\0')
            return false;

        const size_t  n    = (size_t)(close - line) + 1;
        const uint8_t want = (uint8_t)std::strtoul(close + 2, nullptr, 16);
        if (crc8::compute(line, n) != want)
            return false;

        *json    = line;
        *jsonLen = n;
        return true;
    }

    struct Field
    {
        enum class Kind : uint8_t { Missing, String, Number, Bool, Null };

        Kind        kind = Kind::Missing;
        const char *ptr  = nullptr; // String: first char inside the quotes
        size_t      len  = 0;       // String: length without quotes
        double      num  = 0.0;
        bool        flag = false;
    };

    inline const char *skipWs(const char *p, const char *end)
    {
        while (p < end && (*p == ' ' || *p == '\t'))
            ++p;
        return p;
    }

    inline Field find(const char *json, size_t jsonLen, const char *key)
    {
        Field f;
        char needle[24];
        std::snprintf(needle, sizeof(needle), "\"%s\"", key);

        const char *end = json + jsonLen;
        const char *hit = std::strstr(json, needle);
        if (!hit || hit >= end)
            return f;

        const char *p = hit + std::strlen(needle);
        p = skipWs(p, end);
        if (p >= end || *p != ':')
            return f;
        p = skipWs(p + 1, end);
        if (p >= end)
            return f;

        if (*p == '"')
        {
            const char *q = ++p;
            while (q < end && *q != '"')
                ++q;
            if (q >= end)
                return f;
            f.kind = Field::Kind::String;
            f.ptr  = p;
            f.len  = (size_t)(q - p);
            return f;
        }
        if (!std::strncmp(p, "true", 4))  { f.kind = Field::Kind::Bool; f.flag = true;  return f; }
        if (!std::strncmp(p, "false", 5)) { f.kind = Field::Kind::Bool; f.flag = false; return f; }
        if (!std::strncmp(p, "null", 4))  { f.kind = Field::Kind::Null; return f; }

        char *numEnd = nullptr;
        const double v = std::strtod(p, &numEnd);
        if (numEnd == p)
            return f;
        f.kind = Field::Kind::Number;
        f.num  = v;
        return f;
    }

    inline bool getStr(const char *json, size_t jsonLen, const char *key, char *out, size_t cap)
    {
        const Field f = find(json, jsonLen, key);
        if (f.kind != Field::Kind::String || cap == 0)
            return false;
        const size_t n = (f.len < cap - 1) ? f.len : cap - 1;
        std::memcpy(out, f.ptr, n);
        out[n] = '\0';
        return true;
    }

    inline long getInt(const char *json, size_t jsonLen, const char *key, long def)
    {
        const Field f = find(json, jsonLen, key);
        return (f.kind == Field::Kind::Number) ? (long)f.num : def;
    }

    // Append "*XX" for the object already written into `line` (ending at '}').
    inline void seal(char *line, size_t cap)
    {
        const size_t n = std::strlen(line);
        if (n == 0 || n + 4 >= cap)
            return;
        const uint8_t crc = crc8::compute(line, n);
        std::snprintf(line + n, cap - n, "*%02X", crc);
    }

    // Build a sealed cfg.state acknowledgement (docs/protocol.md §3.3). `vLit` is
    // a JSON value literal - "\"AUTO\"", "40", "true", "null". `key`/`err` may be
    // null. Used by both link_uart (src "uart") and ui (src "button").
    inline void cfgState(char *line, size_t cap, const char *key, const char *vLit,
                         long id, bool ok, const char *err, const char *src, unsigned ver)
    {
        char kbuf[40];
        if (key && *key)
            std::snprintf(kbuf, sizeof(kbuf), "\"%s\"", key);
        else
            std::strcpy(kbuf, "null");

        char ebuf[24];
        if (err)
            std::snprintf(ebuf, sizeof(ebuf), "\"%s\"", err);
        else
            std::strcpy(ebuf, "null");

        std::snprintf(line, cap,
                      "{\"t\":\"cfg.state\",\"k\":%s,\"v\":%s,\"id\":%ld,\"ok\":%s,"
                      "\"err\":%s,\"src\":\"%s\",\"ver\":%u}",
                      kbuf, vLit, id, ok ? "true" : "false", ebuf, src, ver);
        seal(line, cap);
    }
}
