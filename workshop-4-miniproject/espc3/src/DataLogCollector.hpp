#pragma once
#include "hardware/SpiBus.hpp"
#include "hardware/SpiDevice.hpp"
#include "hardware/StatusLed.hpp"
#include "logs/LogsTarget.hpp"
#include "logs/TargetTimer.hpp"
#include "LogProtocol.hpp"
#include <esp_log.h>
#include <esp_attr.h>
#include <esp_timer.h>
#include <cstring>

// Polls a predefined set of CS pins, attaching a device just long enough to
// clock one block of its log stream, then detaching. Presence detection and data
// collection are split into two independently-paced entry points:
//
//   drainPresent() — fast path, call continuously with no delay: drains a block
//                    from every currently-present device (marks REMOVED ones).
//   probeAbsent()  — slow path, call periodically (e.g. 1 Hz): probes only the
//                    currently-absent pins for a newly plugged device (ADDED).
//
// Splitting them lets data draining run flat-out (low latency, no drops) while
// hot-plug scanning — which changes rarely — stays cheap and periodic. Both share
// one pipelined sweep(): device i's block is clocked on the wire (DMA, fire-and-
// forget) while device (i-1)'s block is parsed, so at most TWO devices are
// attached at once and only ONE transfer is ever on the wire. Two attached stays
// within the SPI host's concurrent-device limit no matter how many pins are
// listed. Transitions are logged, and per-pin record counts are kept.
//
// The wire protocol is a best-effort one-way stream (see LogProtocol.hpp): the
// master only clocks dummy bytes; the slave feeds self-framed packets from a
// circular DMA buffer. Reading is therefore just "clock a block, parse packets
// out of it." Because consecutive reads of one device are contiguous slices of
// its stream, a per-device accumulator carries a packet that straddles a block
// boundary into the next read. ENTRY packets are de-duplicated by their monotonic
// `seq`, since an idle slave re-presents old packets as its ring laps.
//
// Every collected record is fanned out to all registered LogsTarget sinks, so
// UART, SD card, etc. can consume the same stream simultaneously.
class DataLogCollector
{
public:
    static constexpr int MAX_DEVICES   = 8;
    static constexpr int MAX_TARGETS   = 4;
    // Cap on distinct objectId values tracked per device for the status report.
    // A slave logging more than this many kinds of object just stops growing
    // the set (existing entries are still recognised); plenty for the handful
    // of sensor object ids ("LGHT", "TIME", ...) any one slave here logs.
    static constexpr int MAX_OBJECT_IDS = 8;
    // Bytes clocked out of each device per pass. Larger = more records drained
    // per scan (higher throughput) at the cost of a longer SPI transaction.
    static constexpr int COLLECT_BLOCK = 512;
    // Accumulator must hold last pass's leftover (< one max packet) plus a fresh
    // block, so a straddling packet is always completed.
    static constexpr int ACC_CAP       = COLLECT_BLOCK + logproto::MAX_PACKET;

private:
    SpiBus&    _bus;
    SpiDevice  _dev[MAX_DEVICES];
    gpio_num_t _pin[MAX_DEVICES];
    bool       _present[MAX_DEVICES];
    uint32_t   _records[MAX_DEVICES];  // count delivered (diagnostics)
    uint16_t   _dropped[MAX_DEVICES];  // last-seen slave overflow-drop count
    uint32_t   _invalidTotal[MAX_DEVICES]; // CRC-failed frames seen, this device
    uint32_t   _duplicates[MAX_DEVICES];   // already-delivered seq re-seen (ring lap)
    char       _objectIds[MAX_DEVICES][MAX_OBJECT_IDS][4]; // distinct objectIds seen
    int        _objectIdCount[MAX_DEVICES];                // entries used in _objectIds[i]
    uint32_t   _lastSeq[MAX_DEVICES];  // highest ENTRY seq delivered (dedup)
    bool       _haveSeq[MAX_DEVICES];  // _lastSeq valid yet?
    uint8_t    _acc[MAX_DEVICES][ACC_CAP];
    int        _accLen[MAX_DEVICES];   // valid bytes buffered in _acc[i]
    int        _count;

    LogsTarget* _targets[MAX_TARGETS];
    int         _targetsCount = 0;
    TargetTimer _timer;

    // Optional activity LED: pulsed once per SPI block transfer for a brief
    // "bytes on the wire" blink. Null = no indication (unchanged behaviour).
    StatusLed*  _led = nullptr;

    static constexpr const char* TAG = "COLLECTOR";

    // Fill the shared MOSI scratch buffer once. See the note in scan().
    static void initTxPattern(uint8_t* tx, size_t n)
    {
        // Normal operation: the protocol is one-way (slaves ignore MOSI), so the
        // master just clocks dummy 0xFF bytes to generate SCK. For pin bring-up
        // you can instead fill this with a recognizable non-constant pattern
        // (e.g. 0xDE 0xAD 0xBE 0xEF repeating) to make MOSI visible on a logic
        // analyzer; slaves still ignore it, so parsing is unaffected.
        memset(tx, 0xFF, n);
    }

    // Parse one freshly received block for device i: append it to the per-device
    // accumulator, pull every complete packet out, and dispatch new ENTRY
    // records. Returns true if any valid packet was seen (a live slave is on this
    // pin). `rx` points at the DMA buffer the block was clocked into by queue().
    bool parseBlock(int i, const uint8_t* rx)
    {
        // While a pin has been reading as absent, keep its parse state clear so
        // the first block from a newly plugged device starts clean (no stale
        // leftover bytes and no stale seq high-water mark).
        if (!_present[i])
            resetDevice(i);

        // Append the fresh block to whatever straddled the last read. _accLen is
        // always < MAX_PACKET after a parse, so this never overflows ACC_CAP.
        int copyable = COLLECT_BLOCK;
        if (_accLen[i] + copyable > ACC_CAP)
            copyable = ACC_CAP - _accLen[i];
        memcpy(_acc[i] + _accLen[i], rx, copyable);
        _accLen[i] += copyable;

        // Tallied locally and reported as one consolidated line at the end of
        // this pass (see below), instead of a separate log line per event —
        // both are per-device stats, not per-target, so TargetTimer doesn't
        // see them.
        uint32_t droppedThisPass = 0;
        uint32_t invalidThisPass = 0;

        bool sawValid = false;
        int  pos      = 0;
        while (pos < _accLen[i])
        {
            size_t consumed = 0;
            logproto::ParseResult res =
                logproto::parsePacket(_acc[i] + pos, _accLen[i] - pos, consumed);

            if (res == logproto::PKT_NEED_MORE)
                break;                       // wait for the next block's bytes
            if (res == logproto::PKT_BAD)
            {
                // Only count it as a genuinely invalid *message*, not idle-line
                // noise: parsePacket() also returns PKT_BAD for a plain non-MAGIC
                // byte (0xFF filler between records, or the byte after a torn
                // packet while resyncing), which is expected traffic, not a
                // transmission error. MAGIC having matched but the CRC failing
                // afterward is the real signal something got corrupted on the wire.
                if (_acc[i][pos] == logproto::MAGIC)
                {
                    ++invalidThisPass;
                    ++_invalidTotal[i];
                }
                pos += (int)consumed;        // skip one byte, resync on next MAGIC
                continue;
            }
            sawValid = true;
            droppedThisPass += handlePacket(i, _acc[i] + pos);
            pos += (int)consumed;
        }

        // Keep the unparsed tail (a partial packet) for the next pass.
        _accLen[i] -= pos;
        if (_accLen[i] > 0)
            memmove(_acc[i], _acc[i] + pos, _accLen[i]);

        if (droppedThisPass > 0 || invalidThisPass > 0)
            ESP_LOGW(TAG, "CS%d: %u new dropped (total %u), %u new invalid (total %u)",
                     (int)_pin[i], (unsigned)droppedThisPass, (unsigned)_dropped[i],
                     (unsigned)invalidThisPass, (unsigned)_invalidTotal[i]);

        return sawValid;
    }

    // Process one CRC-valid packet at `p`. Updates the drop counter, and for
    // ENTRY packets de-duplicates by seq and dispatches the record. Returns
    // how many new drops the slave reported in this packet (0 most of the
    // time) so the caller can fold it into one per-pass summary line.
    uint16_t handlePacket(int i, const uint8_t* p)
    {
        logproto::Header h;
        memcpy(&h, p, sizeof(h));            // p may be unaligned; copy out fields

        if (!logproto::isEntry(h))
            return 0;                        // NoEntry heartbeat: presence only

        // The stream re-presents old ENTRY packets as the slave's ring laps, so
        // only deliver a seq we have not delivered before.
        if (_haveSeq[i] && (int32_t)(h.seq - _lastSeq[i]) <= 0)
        {
            ++_duplicates[i];
            return 0;
        }
        if (_haveSeq[i] && h.seq != _lastSeq[i] + 1)
            ESP_LOGW(TAG, "CS%d: seq gap %u -> %u (records missed)",
                     (int)_pin[i], (unsigned)_lastSeq[i], (unsigned)h.seq);
        _lastSeq[i] = h.seq;
        _haveSeq[i] = true;

        // Only ENTRY packets carry a live drop count (NoEntry reports 0). Read it
        // here, after dedup, so it stays monotonic: a rise means the slave
        // discarded records we will never receive.
        uint16_t delta = 0;
        if (h.dropped != _dropped[i])
        {
            delta       = (uint16_t)(h.dropped - _dropped[i]);
            _dropped[i] = h.dropped;
        }

        uint8_t len = h.payloadLen;          // parsePacket already bounds-checked

        LogRecord rec = {};
        rec.cs            = _pin[i];
        rec.seq           = h.seq;
        rec.eventTimeMs   = h.timestamp;
        rec.collectedAtUs = esp_timer_get_time();
        memcpy(rec.objectId, h.objectId, sizeof(rec.objectId));
        rec.valueType     = h.valueType;
        rec.data          = p + logproto::HEADER_SIZE;   // valid until next read
        rec.len           = len;
        dispatch(rec);
        _records[i]++;
        noteObjectId(i, h.objectId);
        return delta;
    }

    // Add h's 4-byte objectId to device i's seen-set if it is not already
    // there and there is room (see MAX_OBJECT_IDS). Only called for records
    // that were actually handled (new, non-duplicate ENTRY), matching what
    // logStatus() reports alongside processed_entries.
    void noteObjectId(int i, const char objectId[4])
    {
        for (int k = 0; k < _objectIdCount[i]; ++k)
            if (memcmp(_objectIds[i][k], objectId, 4) == 0)
                return;
        if (_objectIdCount[i] < MAX_OBJECT_IDS)
            memcpy(_objectIds[i][_objectIdCount[i]++], objectId, 4);
    }

    // Fan one record out to every registered sink, timing each write().
    void dispatch(const LogRecord& rec)
    {
        _timer.dispatch(_targets, _targetsCount, rec);
    }

    // Clear per-device parse/dedup state (called when a device (re)appears so a
    // fresh slave's early records are not skipped and stale bytes are dropped).
    void resetDevice(int i)
    {
        _accLen[i]       = 0;
        _haveSeq[i]      = false;
        _lastSeq[i]      = 0;
        _dropped[i]      = 0;
        _invalidTotal[i] = 0;
        _duplicates[i]   = 0;
        _objectIdCount[i] = 0;
    }

    // Reap device i's previously queued transfer, detach it, then parse the block
    // out of `rx` and update presence (logging ADDED/REMOVED transitions). Called
    // for the pipeline's trailing device while the next device's block is already
    // clocking on the wire.
    void finishTransfer(int i, const uint8_t* rx)
    {
        esp_err_t rr = _dev[i].getResult();   // block until this block's DMA done
        _dev[i].detach();

        bool present;
        if (rr != ESP_OK)
        {
            ESP_LOGE(TAG, "CS%d: transfer failed: %s",
                     (int)_pin[i], esp_err_to_name(rr));
            present = false;
        }
        else
        {
            present = parseBlock(i, rx);
        }

        if (present && !_present[i])
            ESP_LOGI(TAG, "Device ADDED on CS%d", (int)_pin[i]);
        else if (!present && _present[i])
            ESP_LOGI(TAG, "Device REMOVED on CS%d", (int)_pin[i]);
        _present[i] = present;
    }

    // Pipelined (fire-and-forget) sweep over the subset of pins selected by
    // wantPresent: pass true to drain the currently-present devices, false to
    // probe the currently-absent pins for hot-plug. Device i's block clocks on
    // the wire via DMA while device (i-1)'s block is parsed; finishTransfer()
    // updates each pin's presence (logging ADDED/REMOVED). Returns the number of
    // pins reading as present after the sweep.
    //
    // Concurrency invariant (re-checked against the host's device cap, per the
    // project's design notes): only ONE transfer is ever on the wire (single
    // shared bus, serialized by the driver), and AT MOST TWO devices are attached
    // at once — device i is attached before device (i-1) is detached inside
    // finishTransfer(). Two is well within the C3's 3 hardware-CS slots.
    int sweep(bool wantPresent)
    {
        // Shared DMA scratch. tx is constant and read-only, so both in-flight
        // transactions can point at it; rx is double-buffered (ping-pong) so
        // parsing the previous block never races the DMA filling the next one.
        // Static and shared across pins — safe because the collector runs on one
        // task (both drainPresent() and probeAbsent() funnel through here).
        static DMA_ATTR uint8_t tx[COLLECT_BLOCK];
        static DMA_ATTR uint8_t rx[2][COLLECT_BLOCK];
        static bool txInit = false;
        if (!txInit)
        {
            initTxPattern(tx, sizeof(tx));
            txInit = true;
        }

        // Transaction descriptors for the two in-flight slots. They must outlive
        // their transfers, so they live for the whole sweep. They ping-pong in
        // lockstep with rx[]: at most two are unreaped at any instant. buf only
        // advances on devices actually queued, so skipped pins don't disturb the
        // ping-pong parity.
        spi_transaction_t trans[2];

        int inflightIdx = -1;   // device with a queued, not-yet-reaped transfer
        int inflightBuf = 0;    // which rx[]/trans[] slot it occupies
        int buf         = 0;    // next ping-pong slot to use

        for (int i = 0; i < _count; ++i)
        {
            if (_present[i] != wantPresent)   // not in this sweep's subset
                continue;

            // Start device i's transfer without waiting for it (fire-and-forget).
            bool queued = false;
            esp_err_t ar = _dev[i].attach();
            if (ar != ESP_OK)
            {
                ESP_LOGE(TAG, "CS%d: attach failed: %s",
                         (int)_pin[i], esp_err_to_name(ar));
            }
            else
            {
                if (_led)                 // bytes are about to be clocked out
                    _led->pulse();
                esp_err_t qr =
                    _dev[i].queue(&trans[buf], tx, rx[buf], COLLECT_BLOCK);
                if (qr != ESP_OK)
                {
                    ESP_LOGE(TAG, "CS%d: queue failed: %s",
                             (int)_pin[i], esp_err_to_name(qr));
                    _dev[i].detach();
                }
                else
                {
                    queued = true;
                }
            }

            // While device i clocks on the wire, reap + parse the previous one.
            if (inflightIdx >= 0)
                finishTransfer(inflightIdx, rx[inflightBuf]);

            // Device i (if it started) becomes the new in-flight slot. Its buffer
            // was freed one queued device ago, when its same-parity predecessor
            // was reaped above.
            if (queued)
            {
                inflightIdx = i;
                inflightBuf = buf;
                buf ^= 1;
            }
            else
            {
                inflightIdx = -1;
            }
        }

        // Drain the final in-flight transfer.
        if (inflightIdx >= 0)
            finishTransfer(inflightIdx, rx[inflightBuf]);

        int present = 0;
        for (int i = 0; i < _count; ++i)
            if (_present[i])
                ++present;
        return present;
    }

public:
    DataLogCollector(SpiBus& bus, const gpio_num_t* csPins, int count,
                     int clockHz = 1'000'000)
        : _bus(bus), _count(count < MAX_DEVICES ? count : MAX_DEVICES)
    {
        if (count > MAX_DEVICES)
            ESP_LOGE(TAG, "%d CS pins given, only %d supported (MAX_DEVICES); "
                          "the rest are silently dropped", count, MAX_DEVICES);
        for (int i = 0; i < _count; ++i)
        {
            _pin[i]     = csPins[i];
            _dev[i]     = SpiDevice(bus.host(), csPins[i], clockHz);
            _present[i] = false;
            _records[i] = 0;
            resetDevice(i);
        }
    }

    // Register a sink. init() is called here; on failure the target is not
    // added. Returns false if it failed or there is no room.
    bool addTarget(LogsTarget* target)
    {
        if (_targetsCount >= MAX_TARGETS)
        {
            ESP_LOGE(TAG, "target list full, '%s' rejected", target->name());
            return false;
        }
        if (!target->init())
        {
            ESP_LOGE(TAG, "target '%s' init failed, not added", target->name());
            return false;
        }
        _targets[_targetsCount++] = target;
        ESP_LOGI(TAG, "target '%s' added (%d total)", target->name(), _targetsCount);
        return true;
    }

    // Attach an activity LED that blinks briefly on every SPI block transfer.
    // Optional — the collector works the same without one.
    void setStatusLed(StatusLed* led) { _led = led; }

    // Fast path — call as often as you like, with no delay in between. Drains
    // one block from every currently-present device (pipelined). A device that
    // stops presenting valid packets is marked REMOVED here. Returns the number
    // of devices still present. While ≥1 device is present each call performs SPI
    // transfers, and getResult() blocks the task during each one, so a tight
    // caller loop naturally yields the CPU — no explicit delay needed.
    int drainPresent() { return sweep(true); }

    // Slow path — call periodically (e.g. once per second). Probes only the
    // currently-absent pins for a newly plugged device, marking it ADDED. Kept
    // separate from drainPresent() so hot-plug detection (rare) does not gate
    // continuous draining (constant). Returns the number of devices present.
    int probeAbsent() { return sweep(false); }

    int  deviceCount() const      { return _count; }
    bool present(int i) const     { return _present[i]; }
    uint32_t records(int i) const { return _records[i]; }
    uint32_t dropped(int i) const { return _dropped[i]; }

    void logStatus() const
    {
        uint32_t processedEntries = 0;
        uint32_t droppedMessages = 0;
        int presentCount = 0;

        for (int i = 0; i < _count; ++i)
        {
            processedEntries += _records[i];
            droppedMessages += _dropped[i];
            if (_present[i])
                ++presentCount;
        }

        ESP_LOGI(TAG, "STATUS: present=%d/%d processed_entries=%u dropped_messages=%u",
                 presentCount, _count, (unsigned)processedEntries,
                 (unsigned)droppedMessages);

        for (int i = 0; i < _count; ++i)
        {
            char objIds[MAX_OBJECT_IDS * 5]; // "XXXX," per entry, worst case
            formatObjectIds(i, objIds, sizeof(objIds));

            ESP_LOGI(TAG, "  CS%d: present=%d processed_entries=%u duplicates=%u "
                          "dropped_messages=%u invalid_messages=%u unique_objectids=%d [%s]",
                     (int)_pin[i], _present[i] ? 1 : 0,
                     (unsigned)_records[i], (unsigned)_duplicates[i],
                     (unsigned)_dropped[i], (unsigned)_invalidTotal[i],
                     _objectIdCount[i], objIds);
        }
    }

private:
    // Render device i's seen objectId set as a comma-separated list, e.g.
    // "LGHT,TIME". Each id is exactly 4 raw chars (not NUL-terminated in
    // storage), hence the explicit %.4s rather than treating it as a C string.
    void formatObjectIds(int i, char* out, size_t cap) const
    {
        int n = 0;
        for (int k = 0; k < _objectIdCount[i] && n < (int)cap; ++k)
        {
            if (k > 0)
                n += snprintf(out + n, cap - n, ",");
            if (n < (int)cap)
                n += snprintf(out + n, cap - n, "%.4s", _objectIds[i][k]);
        }
        if (n < (int)cap)
            out[n] = '\0';
        else
            out[cap - 1] = '\0';
    }
};
