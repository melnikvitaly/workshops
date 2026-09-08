#pragma once
#include <cstdlib>
#include <cmath>

// Control-path ASCII grammar over the EYE link. One frame per line
// (docs/protocol.md §2). NDJSON config/telemetry lines start with '{' and are
// handled by Ndjson.hpp instead.
//
// Downlink (EYE -> AIM):
//     E <dx> <dy> <valid>          tracking error, streamed          (AUTO channel)
//     M <vpan> <vtilt>             direct velocity command, deg/s    (MANUAL channel)
//     F                            fire one shot (blank the beam)
//     K <axis> <kp> <ki> <kd>      set PID gains live; axis = p | t | b
//     N <dpan> <dtilt>             nudge open-loop, in degrees
//     T <0|1>                      telemetry stream off / on
//     Q                            query gains and state
//
// Uplink (AIM -> EYE):
//     G pan ... tilt ... armed ...   gains report (reply to K or Q)
//
// A rejected line is discarded whole - no partial application (§2.2). `reject`
// says why, so the caller can count it: `Range` -> `out_of_range` (a field
// failed §2.3, NaN/inf included), anything else -> `unparsed`.
//
// Deliberately free of hardware, FreeRTOS and config dependencies.
namespace protocol
{
    // The protocol line cap is 256 bytes (docs/protocol.md §3.1).
    constexpr int MAX_LINE = 256;

    // §2.3 bounds.
    constexpr float ERR_LIMIT    = 1.0f;
    constexpr float GAIN_MIN     = 0.0f;
    constexpr float GAIN_MAX     = 1000.0f;
    constexpr float NUDGE_LIMIT  = 30.0f;
    constexpr float MANUAL_LIMIT = 1000.0f;

    enum class FrameType
    {
        Invalid,
        Error,     // 'E'
        ManualVel, // 'M'
        Fire,      // 'F'
        SetGains,  // 'K'
        Nudge,     // 'N'
        Telemetry, // 'T'
        Query,     // 'Q'
    };

    enum class Axis { Pan, Tilt, Both };

    enum class Reject { None, Malformed, Range };

    struct Frame
    {
        FrameType type   = FrameType::Invalid;
        Reject    reject = Reject::Malformed; // meaningful only when type == Invalid

        float dx = 0.0f; // Error: error x. ManualVel: vpan. Nudge: dpan.
        float dy = 0.0f; // Error: error y. ManualVel: vtilt. Nudge: dtilt.

        bool targetVisible = false; // Error only
        bool on            = false; // Telemetry only

        Axis  axis = Axis::Both; // SetGains only
        float kp   = 0.0f;
        float ki   = 0.0f;
        float kd   = 0.0f;
    };

    namespace detail
    {
        inline const char *skipSpace(const char *p)
        {
            while (*p == ' ' || *p == '\t')
                ++p;
            return p;
        }

        // Read `count` numbers into dst. `Range` if a token is NaN/inf (§2.3
        // rejects those explicitly, before any bounds test); `Malformed` if a
        // token is missing or not a number.
        inline Reject readFloats(const char *&p, float *dst, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                char       *end = nullptr;
                const float v   = strtof(p, &end);
                if (end == p)
                    return Reject::Malformed;
                if (!std::isfinite(v))
                    return Reject::Range;
                p      = end;
                dst[i] = v;
            }
            return Reject::None;
        }

        inline bool readLong(const char *&p, long &out)
        {
            char      *end = nullptr;
            const long v   = strtol(p, &end, 10);
            if (end == p)
                return false;
            p   = end;
            out = v;
            return true;
        }

        inline bool atEnd(const char *p) { return *skipSpace(p) == '\0'; }

        inline bool inRange(float v, float lo, float hi) { return v >= lo && v <= hi; }

        inline Frame malformed() { Frame f; f.reject = Reject::Malformed; return f; }
        inline Frame outOfRange() { Frame f; f.reject = Reject::Range; return f; }
    } // namespace detail

    inline Frame parse(const char *line)
    {
        using namespace detail;

        Frame       f;
        const char *p   = skipSpace(line);
        const char  tag = *p;
        if (tag == '\0')
            return malformed();
        ++p;

        switch (tag)
        {
        case 'E':
        case 'e':
        {
            float v[2];
            const Reject r = readFloats(p, v, 2);
            if (r == Reject::Malformed)
                return malformed();
            if (r == Reject::Range)
                return outOfRange();
            long valid = 0;
            if (!readLong(p, valid) || !atEnd(p))
                return malformed();
            if (!inRange(v[0], -ERR_LIMIT, ERR_LIMIT) ||
                !inRange(v[1], -ERR_LIMIT, ERR_LIMIT) ||
                (valid != 0 && valid != 1))
                return outOfRange();
            f.type          = FrameType::Error;
            f.dx            = v[0];
            f.dy            = v[1];
            f.targetVisible = (valid == 1);
            return f;
        }

        case 'M':
        case 'm':
        {
            float v[2];
            const Reject r = readFloats(p, v, 2);
            if (r == Reject::Malformed || !atEnd(p))
                return malformed();
            if (r == Reject::Range)
                return outOfRange();
            if (!inRange(v[0], -MANUAL_LIMIT, MANUAL_LIMIT) ||
                !inRange(v[1], -MANUAL_LIMIT, MANUAL_LIMIT))
                return outOfRange();
            f.type = FrameType::ManualVel;
            f.dx   = v[0];
            f.dy   = v[1];
            return f;
        }

        case 'F':
        case 'f':
            if (!atEnd(p))
                return malformed();
            f.type = FrameType::Fire;
            return f;

        case 'K':
        case 'k':
        {
            const char a = *skipSpace(p);
            if (a == 'p' || a == 'P')      f.axis = Axis::Pan;
            else if (a == 't' || a == 'T') f.axis = Axis::Tilt;
            else if (a == 'b' || a == 'B') f.axis = Axis::Both;
            else                           return malformed();
            p = skipSpace(p) + 1;

            float g[3];
            const Reject r = readFloats(p, g, 3);
            if (r == Reject::Malformed || !atEnd(p))
                return malformed();
            if (r == Reject::Range)
                return outOfRange();
            if (!inRange(g[0], GAIN_MIN, GAIN_MAX) ||
                !inRange(g[1], GAIN_MIN, GAIN_MAX) ||
                !inRange(g[2], GAIN_MIN, GAIN_MAX))
                return outOfRange();
            f.type = FrameType::SetGains;
            f.kp = g[0]; f.ki = g[1]; f.kd = g[2];
            return f;
        }

        case 'N':
        case 'n':
        {
            float v[2];
            const Reject r = readFloats(p, v, 2);
            if (r == Reject::Malformed || !atEnd(p))
                return malformed();
            if (r == Reject::Range)
                return outOfRange();
            if (!inRange(v[0], -NUDGE_LIMIT, NUDGE_LIMIT) ||
                !inRange(v[1], -NUDGE_LIMIT, NUDGE_LIMIT))
                return outOfRange();
            f.type = FrameType::Nudge;
            f.dx = v[0]; f.dy = v[1];
            return f;
        }

        case 'T':
        case 't':
        {
            long val = 0;
            if (!readLong(p, val) || !atEnd(p))
                return malformed();
            if (val != 0 && val != 1)
                return outOfRange();
            f.type = FrameType::Telemetry;
            f.on   = (val == 1);
            return f;
        }

        case 'Q':
        case 'q':
            if (!atEnd(p))
                return malformed();
            f.type = FrameType::Query;
            return f;

        default:
            return malformed();
        }
    }
} // namespace protocol
