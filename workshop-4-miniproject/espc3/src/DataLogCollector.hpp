#pragma once
#include "hardware/SpiBus.hpp"
#include "hardware/SpiDevice.hpp"
#include "logs/LogsTarget.hpp"
#include "LogProtocol.hpp"
#include <esp_log.h>
#include <esp_attr.h>
#include <esp_timer.h>
#include <cstring>

// Polls a predefined set of CS pins. Each scan() pass, for every pin, it
// attaches a device just long enough to clock one block of its log stream, then
// detaches — so at most one device is on the bus at any instant and the SPI
// host's concurrent-device limit is never a concern no matter how many pins are
// listed. Transitions (a device appearing on or vanishing from a pin) are
// logged, and per-pin record counts are kept.
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
    uint32_t   _lastSeq[MAX_DEVICES];  // highest ENTRY seq delivered (dedup)
    bool       _haveSeq[MAX_DEVICES];  // _lastSeq valid yet?
    uint8_t    _acc[MAX_DEVICES][ACC_CAP];
    int        _accLen[MAX_DEVICES];   // valid bytes buffered in _acc[i]
    int        _count;

    LogsTarget* _target[MAX_TARGETS];
    int         _targetCount = 0;

    static constexpr const char* TAG = "COLLECTOR";

    // Clock COLLECT_BLOCK bytes out of the device (dummy 0xFF on MOSI) and read
    // its MISO stream into a DMA-capable buffer. Both scratch buffers are static
    // and shared across pins — safe because scan() runs on a single task.
    esp_err_t readBlock(SpiDevice& d, const uint8_t*& rx)
    {
        static DMA_ATTR uint8_t tx[COLLECT_BLOCK];
        static DMA_ATTR uint8_t rxbuf[COLLECT_BLOCK];
        static bool txInit = false;
        if (!txInit) { memset(tx, 0xFF, sizeof(tx)); txInit = true; }

        esp_err_t r = d.transfer(tx, rxbuf, COLLECT_BLOCK);
        rx = rxbuf;
        return r;
    }

    // Read one block, parse every complete packet out of the accumulator, and
    // dispatch new ENTRY records. Returns true if any valid packet was seen
    // (i.e. a live slave is present on this pin).
    bool collect(int i)
    {
        const uint8_t* rx = nullptr;
        if (readBlock(_dev[i], rx) != ESP_OK)
            return false;

        // Append the fresh block to whatever straddled the last read. _accLen is
        // always < MAX_PACKET after a parse, so this never overflows ACC_CAP.
        int copyable = COLLECT_BLOCK;
        if (_accLen[i] + copyable > ACC_CAP)
            copyable = ACC_CAP - _accLen[i];
        memcpy(_acc[i] + _accLen[i], rx, copyable);
        _accLen[i] += copyable;

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
                pos += (int)consumed;        // skip one byte, resync on next MAGIC
                continue;
            }
            sawValid = true;
            handlePacket(i, _acc[i] + pos);
            pos += (int)consumed;
        }

        // Keep the unparsed tail (a partial packet) for the next pass.
        _accLen[i] -= pos;
        if (_accLen[i] > 0)
            memmove(_acc[i], _acc[i] + pos, _accLen[i]);
        return sawValid;
    }

    // Process one CRC-valid packet at `p`. Updates the drop counter, and for
    // ENTRY packets de-duplicates by seq and dispatches the record.
    void handlePacket(int i, const uint8_t* p)
    {
        logproto::Header h;
        memcpy(&h, p, sizeof(h));            // p may be unaligned; copy out fields

        if (!logproto::isEntry(h))
            return;                          // NoEntry heartbeat: presence only

        // The stream re-presents old ENTRY packets as the slave's ring laps, so
        // only deliver a seq we have not delivered before.
        if (_haveSeq[i] && (int32_t)(h.seq - _lastSeq[i]) <= 0)
            return;
        if (_haveSeq[i] && h.seq != _lastSeq[i] + 1)
            ESP_LOGW(TAG, "CS%d: seq gap %u -> %u (records missed)",
                     (int)_pin[i], (unsigned)_lastSeq[i], (unsigned)h.seq);
        _lastSeq[i] = h.seq;
        _haveSeq[i] = true;

        // Only ENTRY packets carry a live drop count (NoEntry reports 0). Read it
        // here, after dedup, so it stays monotonic: a rise means the slave
        // discarded records we will never receive.
        if (h.dropped != _dropped[i])
        {
            ESP_LOGW(TAG, "CS%d: slave dropped %u records (buffer overflow)",
                     (int)_pin[i], (unsigned)(uint16_t)(h.dropped - _dropped[i]));
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
    }

    // Fan one record out to every registered sink.
    void dispatch(const LogRecord& rec)
    {
        for (int t = 0; t < _targetCount; ++t)
            _target[t]->write(rec);
    }

    // Clear per-device parse/dedup state (called when a device (re)appears so a
    // fresh slave's early records are not skipped and stale bytes are dropped).
    void resetDevice(int i)
    {
        _accLen[i]  = 0;
        _haveSeq[i] = false;
        _lastSeq[i] = 0;
        _dropped[i] = 0;
    }

public:
    DataLogCollector(SpiBus& bus, const gpio_num_t* csPins, int count,
                     int clockHz = 1'000'000)
        : _bus(bus), _count(count < MAX_DEVICES ? count : MAX_DEVICES)
    {
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
        if (_targetCount >= MAX_TARGETS)
        {
            ESP_LOGE(TAG, "target list full, '%s' rejected", target->name());
            return false;
        }
        if (!target->init())
        {
            ESP_LOGE(TAG, "target '%s' init failed, not added", target->name());
            return false;
        }
        _target[_targetCount++] = target;
        ESP_LOGI(TAG, "target '%s' added (%d total)", target->name(), _targetCount);
        return true;
    }

    // One full sweep over all predefined CS pins.
    void scan()
    {
        for (int i = 0; i < _count; ++i)
        {
            esp_err_t r = _dev[i].attach();
            if (r != ESP_OK)
            {
                ESP_LOGE(TAG, "CS%d: attach failed: %s",
                         (int)_pin[i], esp_err_to_name(r));
                continue;
            }

            // While a pin reads as absent, keep its parse state clear so the
            // first block from a newly plugged device starts clean (no stale
            // leftover bytes and no stale seq high-water mark).
            if (!_present[i])
                resetDevice(i);

            const bool present = collect(i);

            if (present && !_present[i])
                ESP_LOGI(TAG, "Device ADDED on CS%d", (int)_pin[i]);
            else if (!present && _present[i])
                ESP_LOGI(TAG, "Device REMOVED on CS%d", (int)_pin[i]);
            _present[i] = present;

            _dev[i].detach();
        }
    }

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
            ESP_LOGI(TAG, "  CS%d: present=%d processed_entries=%u dropped_messages=%u",
                     (int)_pin[i], _present[i] ? 1 : 0,
                     (unsigned)_records[i], (unsigned)_dropped[i]);
        }
    }
};
