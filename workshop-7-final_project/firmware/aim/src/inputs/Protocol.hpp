#pragma once
#include <cstdlib>
#include <cmath>

// Wire protocol spoken over UART. One frame per line.
//
// Downlink (PC -> ESP32):
//     E <dx> <dy> <valid>\n        tracking error, streamed
//     F\n                          fire one shot (blank the beam briefly)
//     K <axis> <kp> <ki> <kd>\n    set PID gains live; axis = p | t | b(oth)
//     N <dpan> <dtilt>\n           nudge the gimbal open-loop, in degrees
//     T <0|1>\n                    telemetry stream off / on
//     Q\n                          print current gains and state
//
// Uplink (ESP32 -> PC):
//     G pan ... tilt ... armed ...\n  gains report (in reply to K or Q)
//     T ex:... ey:... ... st:... arr:<0|1>\n telemetry sample (while enabled)
//
// The authoritative contract is docs/uart-protocol.md; this file is its
// implementation and the two must be kept in step.
//
// Deliberately free of hardware, FreeRTOS and config dependencies: parsing a
// line is pure text handling, and keeping it that way means the wire format can
// be reasoned about (and unit-tested on a host) in isolation from the control
// loop that consumes it. What belongs here is anything the *contract* dictates;
// what belongs to the caller is anything the *mounting or tuning* dictates -
// so axis inversion and the control deadzone stay in ErrorVectorInput.
namespace protocol
{
    // Longest line accepted, including the terminator. Anything longer is
    // discarded through to the next newline rather than truncated: acting on
    // half a coordinate pair would aim the gimbal somewhere arbitrary.
    constexpr int MAX_LINE = 96;

    enum class FrameType
    {
        Invalid,   // unparseable - console log text, noise, a partial line
        Error,     // 'E' - a tracking measurement
        Fire,      // 'F' - one-shot laser trigger
        SetGains,  // 'K' - live PID retune
        Nudge,     // 'N' - open-loop displacement, for step-response tests
        Telemetry, // 'T' - turn the per-frame log line on or off
        Query,     // 'Q' - dump current gains and state
    };

    enum class Axis
    {
        Pan,
        Tilt,
        Both,
    };

    struct Frame
    {
        FrameType type = FrameType::Invalid;

        // Error: the measured error. Nudge: degrees of displacement.
        float dx = 0.0f;
        float dy = 0.0f;

        bool targetVisible = false; // Error only - the frame's `valid` field
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

        // Out-of-range errors are clamped rather than rejected, so the PC side
        // never has to clip. Documented behaviour, not a safety net.
        inline float clampUnit(float v)
        {
            if (v < -1.0f)
                return -1.0f;
            if (v > 1.0f)
                return 1.0f;
            return v;
        }

        // Read a float, advancing p. Returns false if there was no number.
        inline bool readFloat(const char *&p, float &out)
        {
            char       *end = nullptr;
            const float v   = strtof(p, &end);
            if (end == p || !std::isfinite(v))
                return false;
            p   = end;
            out = v;
            return true;
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

        // True when only whitespace remains. Used to reject trailing garbage:
        // the console shares this UART, so log text arrives on the same wire and
        // a line that merely *starts* with a valid tag must not be acted on.
        inline bool atEnd(const char *p) { return *skipSpace(p) == '\0'; }
    } // namespace detail

    // Classify and decode one complete line. Returns a Frame whose type says
    // what was found; Invalid means the line was not ours and should be
    // counted and dropped, never partially acted on.
    inline Frame parse(const char *line)
    {
        using namespace detail;

        Frame       f;
        const char *p   = skipSpace(line);
        const char  tag = *p;
        if (tag == '\0')
            return f;
        ++p;

        switch (tag)
        {
        case 'E':
        case 'e':
        {
            long valid = 0;
            if (!readFloat(p, f.dx) || !readFloat(p, f.dy) || !readLong(p, valid))
                return Frame{};
            f.type          = FrameType::Error;
            f.dx            = clampUnit(f.dx);
            f.dy            = clampUnit(f.dy);
            f.targetVisible = (valid != 0);
            return f;
        }

        case 'F':
        case 'f':
            // Nothing may follow: no log line starting with an F may fire the
            // laser, and 'F 1' is a typo rather than an instruction.
            if (!atEnd(p))
                return Frame{};
            f.type = FrameType::Fire;
            return f;

        case 'K':
        case 'k':
        {
            const char a = *skipSpace(p);
            if (a == 'p' || a == 'P')
                f.axis = Axis::Pan;
            else if (a == 't' || a == 'T')
                f.axis = Axis::Tilt;
            else if (a == 'b' || a == 'B')
                f.axis = Axis::Both;
            else
                return Frame{};
            p = skipSpace(p) + 1;

            if (!readFloat(p, f.kp) || !readFloat(p, f.ki) || !readFloat(p, f.kd))
                return Frame{};
            if (f.kp < 0.0f || f.ki < 0.0f || f.kd < 0.0f)
                return Frame{}; // negative gains invert the loop - never useful
            f.type = FrameType::SetGains;
            return f;
        }

        case 'N':
        case 'n':
            if (!readFloat(p, f.dx) || !readFloat(p, f.dy))
                return Frame{};
            f.type = FrameType::Nudge;
            return f;

        case 'T':
        case 't':
        {
            long v = 0;
            if (!readLong(p, v))
                return Frame{};
            f.type = FrameType::Telemetry;
            f.on   = (v != 0);
            return f;
        }

        case 'Q':
        case 'q':
            if (!atEnd(p))
                return Frame{};
            f.type = FrameType::Query;
            return f;

        default:
            return f; // Invalid
        }
    }
} // namespace protocol
