#pragma once
#include "hardware/SpiBus.hpp"
#include "hardware/SpiDevice.hpp"
#include "logs/LogsTarget.hpp"
#include "LogProtocol.hpp"
#include <esp_log.h>
#include <esp_attr.h>
#include <esp_timer.h>
#include <esp_rom_sys.h>
#include <cstring>

// Polls a predefined set of CS pins. Each scan() pass, for every pin, it
// attaches a device just long enough to probe it and drain its log queue, then
// detaches — so at most one device is on the bus at any instant and the SPI
// host's concurrent-device limit is never a concern no matter how many pins are
// listed. Transitions (a device appearing on or vanishing from a pin) are
// logged, and per-pin record counts are kept.
//
// Every collected record is fanned out to all registered LogsTarget sinks, so
// UART, SD card, etc. can consume the same stream simultaneously.
class DataLogCollector
{
public:
    static constexpr int MAX_DEVICES          = 8;
    static constexpr int MAX_TARGETS          = 4;
    static constexpr int MAX_RECORDS_PER_SCAN = 16;    // drain cap per pin per pass
    static constexpr uint32_t TIME_SYNC_MS    = 60'000; // re-push clock this often
    static constexpr uint32_t SLAVE_TURNAROUND_US = 100; // phase-1→phase-2 gap

private:
    SpiBus&    _bus;
    SpiDevice  _dev[MAX_DEVICES];
    gpio_num_t _pin[MAX_DEVICES];
    bool       _present[MAX_DEVICES];
    uint32_t   _lastId[MAX_DEVICES];   // highest record id received & delivered
    uint32_t   _records[MAX_DEVICES];  // count delivered (diagnostics)
    uint16_t   _dropped[MAX_DEVICES];  // last-seen slave overflow-drop count
    uint32_t   _lastSyncMs[MAX_DEVICES]; // master-clock ms of last CMD_SET_TIME
    int        _count;

    LogsTarget* _target[MAX_TARGETS];
    int         _targetCount = 0;

    static constexpr const char* TAG = "COLLECTOR";

    // Master-as-reference clock: ms since boot. Swap for SNTP/RTC epoch later
    // and every record becomes true wall-clock with no protocol change.
    static uint32_t masterNowMs()
    {
        return (uint32_t)(esp_timer_get_time() / 1000);
    }

    // Two-phase request/response (see LogProtocol.hpp): phase 1 delivers the
    // command (MISO ignored — the slave is latching it); phase 2 clocks out the
    // reply the slave prepared, using a CMD_NOP that changes nothing. arg is the
    // u32 carried by CMD_GET_LOG (lastReceivedId) / CMD_SET_TIME (masterTimeMs).
    // Static DMA-capable scratch buffers: scan() runs on a single task, so
    // sharing them across all pins is safe and avoids per-call allocation.
    esp_err_t exchange(SpiDevice& d, uint8_t cmd, uint32_t arg, logproto::Frame* out)
    {
        static DMA_ATTR logproto::Frame tx;
        static DMA_ATTR logproto::Frame rx;

        // Phase 1: send the command; the slave prepares its reply for phase 2.
        memset(&tx, 0, sizeof(tx));
        tx.req.cmd = cmd;
        tx.req.arg = arg;
        logproto::sign(tx);
        esp_err_t r = d.transfer(tx.bytes, rx.bytes, logproto::FRAME_SIZE);
        if (r != ESP_OK)
            return r;

        // Give the slave's ISR time to build the reply and re-arm its SPI.
        esp_rom_delay_us(SLAVE_TURNAROUND_US);

        // Phase 2: clock the prepared reply out with a no-op request.
        memset(&tx, 0, sizeof(tx));
        tx.req.cmd = logproto::CMD_NOP;
        logproto::sign(tx);
        r = d.transfer(tx.bytes, rx.bytes, logproto::FRAME_SIZE);
        if (r == ESP_OK && out)
            *out = rx;
        return r;
    }

    bool probe(SpiDevice& d)
    {
        logproto::Frame f;
        return exchange(d, logproto::CMD_PING, 0, &f) == ESP_OK &&
               logproto::valid(f);
    }

    // Push the master clock so the slave stamps records on our timebase.
    void pushTime(SpiDevice& d, uint32_t nowMs)
    {
        logproto::Frame f;
        // The ack response is not needed; CMD_SET_TIME is idempotent and the
        // next periodic push corrects a missed one.
        exchange(d, logproto::CMD_SET_TIME, nowMs, &f);
    }

    void drainLogs(int i)
    {
        uint32_t hw = _lastId[i]; // high-water mark: everything through hw is acked
        for (int n = 0; n < MAX_RECORDS_PER_SCAN; ++n)
        {
            logproto::Frame f;
            // The request acks up to hw; the slave replies with the next id > hw.
            if (exchange(_dev[i], logproto::CMD_GET_LOG, hw, &f) != ESP_OK)
                break;
            if (!logproto::valid(f))
            {
                // hw is left unchanged, so this same record is re-requested next
                // pass — nothing is lost or double-acked.
                ESP_LOGW(TAG, "CS%d: corrupt frame, retrying id>%u next pass",
                         (int)_pin[i], (unsigned)hw);
                break;
            }

            // The slave reports its overflow-drop total in every frame; a rise
            // means it discarded records we will never receive.
            const uint16_t dropped = f.resp.dropped;
            if (dropped != _dropped[i])
            {
                ESP_LOGW(TAG, "CS%d: slave dropped %u records (buffer overflow)",
                         (int)_pin[i], (unsigned)(uint16_t)(dropped - _dropped[i]));
                _dropped[i] = dropped;
            }

            if (logproto::statusCode(f.resp) == logproto::ST_NO_DATA)
                break; // nothing newer than hw

            uint8_t len = f.resp.len;
            if (len > logproto::PAYLOAD_MAX)
                len = logproto::PAYLOAD_MAX;

            const uint32_t id = f.resp.recordId;

            LogRecord rec = {};
            rec.cs            = _pin[i];
            rec.seq           = id; // slave record id (sinks dedup on this)
            rec.eventTimeMs   = f.resp.recordTimeMs;
            rec.timeSynced    = logproto::timeSynced(f.resp);
            rec.collectedAtUs = esp_timer_get_time();
            rec.data          = f.resp.payload; // valid until f goes out of scope
            rec.len           = len;
            dispatch(rec);

            // Advance the ack only after the record is validated and delivered.
            hw = id;
            _lastId[i] = hw;
            _records[i]++;
        }
    }

    // Fan one record out to every registered sink.
    void dispatch(const LogRecord& rec)
    {
        for (int t = 0; t < _targetCount; ++t)
            _target[t]->write(rec);
    }

public:
    DataLogCollector(SpiBus& bus, const gpio_num_t* csPins, int count,
                     int clockHz = 1'000'000)
        : _bus(bus), _count(count < MAX_DEVICES ? count : MAX_DEVICES)
    {
        for (int i = 0; i < _count; ++i)
        {
            _pin[i]        = csPins[i];
            _dev[i]        = SpiDevice(bus.host(), csPins[i], clockHz);
            _present[i]    = false;
            _lastId[i]     = 0;
            _records[i]    = 0;
            _dropped[i]    = 0;
            _lastSyncMs[i] = 0;
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

            const bool     present = probe(_dev[i]);
            const uint32_t nowMs   = masterNowMs();

            if (present && !_present[i])
            {
                // A (possibly different) device just appeared: restart its ack
                // stream from 0 so we don't skip a fresh device's early records,
                // and hand it our clock before draining so records are stamped
                // on the master timebase from the start.
                _lastId[i]  = 0;
                _dropped[i] = 0;
                pushTime(_dev[i], nowMs);
                _lastSyncMs[i] = nowMs;
                ESP_LOGI(TAG, "Device ADDED on CS%d", (int)_pin[i]);
            }
            else if (!present && _present[i])
                ESP_LOGI(TAG, "Device REMOVED on CS%d", (int)_pin[i]);
            _present[i] = present;

            if (present)
            {
                // Periodic re-sync corrects drift (and re-arms a slave that
                // rebooted since the last push).
                if (nowMs - _lastSyncMs[i] >= TIME_SYNC_MS)
                {
                    pushTime(_dev[i], nowMs);
                    _lastSyncMs[i] = nowMs;
                }
                drainLogs(i);
            }

            _dev[i].detach();
        }
    }

    int  deviceCount() const           { return _count; }
    bool present(int i) const          { return _present[i]; }
    uint32_t records(int i) const      { return _records[i]; }
};
