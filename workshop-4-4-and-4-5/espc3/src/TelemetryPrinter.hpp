#pragma once
#include "telemetry_packet.h"   // shared wire format, compiled by both ends
#include <cstdio>
#include <cstdint>

// Turns a received frame into the serial-monitor output.
//
// Kept apart from the SPI plumbing on purpose: SpiSlave knows about bytes and
// nothing else, this knows about fields and nothing else, and main.cpp is the
// twenty lines that join them. Swapping the presentation (a CSV line, an OLED
// page, an MQTT publish) touches only this file.
//
// Everything prints with printf to the USB console (stdout). ESP_LOG is used
// only for problems, so the per-frame block stays clean and copy-pasteable
// while anything wrong still carries a level and a tag.
class TelemetryPrinter
{
    // Link statistics. `_lastSeq` is what makes a *missed* frame visible: the
    // link is one-way with no acknowledgement, so a frame that was clocked out
    // while this end was not armed leaves no trace except a hole in the
    // sequence numbers.
    uint32_t _framesOk      = 0;
    uint32_t _framesBad     = 0;
    uint32_t _framesMissed  = 0;
    uint32_t _lastSeq       = 0;
    bool     _haveLastSeq   = false;

    // A scaled integer prints as whole.fraction without ever touching a float:
    // the C3 has no FPU, and %d.%02d is both exact and cheaper than pulling the
    // soft-float formatter into the binary. Negative values need the sign
    // written by hand — at -0.5 °C the integer quotient -50/100 is 0, so a
    // plain "%d.%02d" would print "0.50" for both +0.5 and -0.5.
    static void printScaled(const char* label, int32_t scaled, int scale, const char* unit)
    {
        const int32_t whole = scaled / scale;
        int32_t       frac  = scaled % scale;
        const char*   sign  = (scaled < 0 && whole == 0) ? "-" : "";
        if (frac < 0)
            frac = -frac;
        std::printf("  %-14s %s%ld.%02ld %s\n", label, sign, (long)whole, (long)frac, unit);
    }

public:
    // One validated frame -> one block of output. The payload is unpacked into
    // named local variables first and only then printed: the wire struct is a
    // layout, and everything below reads as the measurement it actually is.
    void print(const TelemetryPayload& p)
    {
        ++_framesOk;

        // --- date and time -------------------------------------------------
        const int year   = 2000 + p.year;   // the wire carries a 2000-based offset
        const int month  = p.month;
        const int day    = p.day;
        const int hour   = p.hour;
        const int minute = p.minute;
        const int second = p.second;

        // --- measurements ---------------------------------------------------
        const int32_t  temperatureC100 = p.tempC100;      // °C * 100
        const uint32_t humidity100     = p.humidity100;   // %RH * 100
        const uint32_t pressurePa      = p.pressurePa;    // Pa
        const uint32_t lightPercent    = p.lightPct;      // 0..100
        const uint32_t lightRaw        = p.lightRaw;      // 0..4095

        // --- bookkeeping -----------------------------------------------------
        const uint32_t seq       = p.seq;
        const uint32_t uptimeMs  = p.uptimeMs;
        const bool     timeOk    = (p.flags & TELEMETRY_FLAG_TIME_VALID)  != 0;
        const bool     envOk     = (p.flags & TELEMETRY_FLAG_ENV_VALID)   != 0;
        const bool     lightOk   = (p.flags & TELEMETRY_FLAG_LIGHT_VALID) != 0;

        // A jump of more than one in the sequence means the master sent frames
        // this end was not armed to receive.
        if (_haveLastSeq && seq > _lastSeq + 1)
            _framesMissed += seq - _lastSeq - 1;
        _lastSeq     = seq;
        _haveLastSeq = true;

        std::printf("\n");
        std::printf("+-- frame #%lu ---------------- master uptime %lu ms --+\n",
                    (unsigned long)seq, (unsigned long)uptimeMs);

        if (timeOk)
        {
            std::printf("  %-14s %04d-%02d-%02d\n", "date", year, month, day);
            std::printf("  %-14s %02d:%02d:%02d\n", "time", hour, minute, second);
        }
        else
        {
            // The master keeps the previous reading in the frame when a sensor
            // fails, so these fields still hold plausible numbers — say plainly
            // that they are not this second's.
            std::printf("  %-14s %04d-%02d-%02d  (RTC silent, last known)\n",
                        "date", year, month, day);
            std::printf("  %-14s %02d:%02d:%02d    (RTC silent, last known)\n",
                        "time", hour, minute, second);
        }

        if (envOk)
        {
            printScaled("temperature", temperatureC100, TELEMETRY_TEMP_SCALE, "C");
            printScaled("humidity",    (int32_t)humidity100, TELEMETRY_HUMIDITY_SCALE, "%RH");
            printScaled("pressure",    (int32_t)pressurePa, TELEMETRY_PRESSURE_PER_HPA, "hPa");
        }
        else
        {
            std::printf("  %-14s %s\n", "temperature", "-- (BME280 not answering)");
            std::printf("  %-14s %s\n", "humidity",    "-- (BME280 not answering)");
            std::printf("  %-14s %s\n", "pressure",    "-- (BME280 not answering)");
        }

        if (lightOk)
            std::printf("  %-14s %lu %%   (raw %lu / %u)\n", "light",
                        (unsigned long)lightPercent, (unsigned long)lightRaw,
                        TELEMETRY_LIGHT_RAW_MAX);
        else
            std::printf("  %-14s %s\n", "light", "-- (no ADC data)");

        std::printf("+-- ok %lu / bad %lu / missed %lu -------------------+\n",
                    (unsigned long)_framesOk, (unsigned long)_framesBad,
                    (unsigned long)_framesMissed);
    }

    // A frame arrived but did not validate. Counted separately from a missed
    // frame: bad means bytes reached this end and were wrong (wiring, clock
    // rate, version skew), missed means nothing reached it at all — different
    // problems with different fixes.
    void countBad() { ++_framesBad; }

    uint32_t framesOk()     const { return _framesOk; }
    uint32_t framesBad()    const { return _framesBad; }
    uint32_t framesMissed() const { return _framesMissed; }
};
