#pragma once
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_timer.h>
#include <esp_log.h>

#include "Ipc.hpp"
#include "CmdQueue.hpp"
#include "StateMachine.hpp"
#include "Config.hpp"
#include "Sdcard.hpp"
#include "ITransport.hpp"
#include "Ndjson.hpp"

// The logger task (docs/architecture.md §2, §5). Lowest priority and the only
// task allowed a long block - an SD card doing internal wear-levelling can stall
// a single write 100-250 ms, which must never land on ctrl.
//
// It drains log_q into a batch buffer, writes whole blocks to an append-only
// LOG.CSV, and f_syncs on a timer - never per record. The four named failure
// conditions (no card, removed, full, write error) each log once, raise an OLED
// flag via ipc.sd, and never stop the loop. The drop-oldest queue and its
// counter (ipc.logDropped) live in logSend().
class LoggerTask
{
public:
    LoggerTask(Ipc &ipc, Sdcard &sd) : _ipc(ipc), _sd(sd) {}

    static void entry(void *arg) { static_cast<LoggerTask *>(arg)->run(); }

    void run()
    {
        ESP_LOGI(TAG, "logger up - SD sink %s", config::SD_LOG_PATH);
        ESP_LOGI(TAG, "CSV: %s", CSV_HEADER);

        LogRecord r;
        for (;;)
        {
            const bool got =
                xQueueReceive(_ipc.logQ, &r, pdMS_TO_TICKS(config::SD_LOOP_TICK_MS)) == pdTRUE;
            const uint64_t now = (uint64_t)esp_timer_get_time();

            if (got)
                appendRow(r);

            serviceMount(now);

            if (_mounted && !_full && _batchLen >= config::SD_BATCH_BYTES)
                flushBatch();

            syncIfDue(now);
            refreshTelemetry(now);
        }
    }

private:
    static constexpr char TAG[]        = "LOG";
    static constexpr char CSV_HEADER[] =
        "seq,t_mono_us,t_wall_iso,state,channel,ex,ey,vpan,vtilt,pan,tilt,flags";
    static constexpr int LAT_BUCKETS   = 16;    // log2 latency histogram for p95
    static constexpr uint64_t FREE_LOW = config::SD_BATCH_MAX_BYTES; // "card full" threshold

    // --- record -> CSV text -------------------------------------------------
    void appendRow(const LogRecord &r)
    {
        if (r.kind == LogRecord::Kind::Boot)
        {
            std::strncpy(_bootReason, r.note, sizeof(_bootReason) - 1);
            _bootReason[sizeof(_bootReason) - 1] = '\0';
            _haveBootReason = true;
            ESP_LOGI(TAG, "BOOT reason=%s", _bootReason);
            if (_file && !_markerWritten)
                writeBootMarker();
            return;
        }

        if (!_mounted || _full || !_file)
            return; // lossy by design: nothing to write to

        char row[176];
        int n = 0;
        switch (r.kind)
        {
        case LogRecord::Kind::Sample:
            // t_wall_iso stays empty in Phase 0 - monotonic time only.
            n = std::snprintf(row, sizeof(row),
                              "%lu,%llu,,%s,%u,%.3f,%.3f,%.2f,%.2f,%.1f,%.1f,%lu\n",
                              (unsigned long)r.seq, (unsigned long long)r.t_mono_us,
                              stateName(r.state), r.channel,
                              (double)r.ex, (double)r.ey, (double)r.vpan, (double)r.vtilt,
                              (double)r.pan, (double)r.tilt, (unsigned long)r.flags);
            break;
        case LogRecord::Kind::Transition:
            ESP_LOGI(TAG, "EVT -> %s (%s)", stateName(r.state), r.note);
            n = std::snprintf(row, sizeof(row), "%lu,%llu,,%s,,,,,,,,%s\n",
                              (unsigned long)r.seq, (unsigned long long)r.t_mono_us,
                              stateName(r.state), r.note);
            break;
        case LogRecord::Kind::Sd:
            n = std::snprintf(row, sizeof(row), "%lu,%llu,,%s,,,,,,,,%s\n",
                              (unsigned long)r.seq, (unsigned long long)r.t_mono_us,
                              stateName(r.state), r.note);
            break;
        case LogRecord::Kind::Boot:
            break; // handled above
        }
        if (n > 0)
            appendText(row);
    }

    void appendText(const char *s)
    {
        const size_t len = std::strlen(s);
        if (_batchLen + len > sizeof(_batch))
        {
            flushBatch(); // make room
            if (_batchLen + len > sizeof(_batch))
                return;   // write stalled - drop this row rather than grow
        }
        std::memcpy(_batch + _batchLen, s, len);
        _batchLen += len;
    }

    // --- block writes ----------------------------------------------------------
    void flushBatch()
    {
        if (!_file || !_mounted || _full || _batchLen == 0)
            return;

        const uint64_t t0 = (uint64_t)esp_timer_get_time();
        const size_t   n  = std::fwrite(_batch, 1, _batchLen, _file);
        recordLatency((uint64_t)esp_timer_get_time() - t0);

        _bytesWindow += n;
        _dirty = true;

        if (n < _batchLen)
        {
            std::memmove(_batch, _batch + n, _batchLen - n);
            _batchLen -= n;
            handleFailure(); // short write - card full or gone
            return;
        }
        _batchLen = 0;
        if (std::ferror(_file))
            handleFailure();
    }

    void syncIfDue(uint64_t now)
    {
        if (!_mounted || _full || !_file)
            return;
        if (now - _lastSyncUs < (uint64_t)config::SD_SYNC_SECONDS * 1000000ull)
            return;
        _lastSyncUs = now;

        flushBatch(); // push whatever is buffered so a sync actually persists it
        if (!_dirty || !_file)
            return;

        if (std::fflush(_file) != 0 || fsync(fileno(_file)) != 0)
        {
            handleFailure();
            return;
        }
        _dirty = false;
        _ipc.sd.syncCount++;
    }

    // --- mount / retry -------------------------------------------------------
    void serviceMount(uint64_t now)
    {
        if (_mounted)
            return;
        if (_lastMountTryUs != 0 &&
            now - _lastMountTryUs < (uint64_t)config::SD_MOUNT_RETRY_MS * 1000ull)
            return;
        _lastMountTryUs = now;

        if (_sd.mount() != ESP_OK)
        {
            _ipc.sd.present = 0;
            _ipc.sd.mounted = 0;
            sdCondition("absent", 0);
            return;
        }
        if (!openLog())
        {
            _sd.unmount();
            sdCondition("mount_err", 0);
            return;
        }
        _mounted = true;
        _full    = false;
        _ipc.sd.present = 1;
        _ipc.sd.mounted = 1;
        _ipc.sd.full    = 0;
        refreshFree();
        sdCondition("mounted", 1);
    }

    bool openLog()
    {
        _file = std::fopen(config::SD_LOG_PATH, "a");
        if (!_file)
        {
            ESP_LOGW(TAG, "fopen %s failed", config::SD_LOG_PATH);
            return false;
        }
        std::fseek(_file, 0, SEEK_END);
        if (std::ftell(_file) == 0)
        {
            std::fputs(CSV_HEADER, _file);
            std::fputc('\n', _file);
        }

        if (!_markerWritten && _haveBootReason)
            writeBootMarker();

        std::fflush(_file);
        _lastSyncUs = (uint64_t)esp_timer_get_time();
        _dirty      = false;
        return true;
    }

    // BOOT marker: seq 0, t_mono_us 0, reset reason in the trailing column so a
    // reader can tell a reboot from a backwards jump in seq (architecture.md §5).
    void writeBootMarker()
    {
        char row[64];
        std::snprintf(row, sizeof(row), "0,0,,BOOT,,,,,,,,%s\n", _bootReason);
        if (_batchLen + std::strlen(row) <= sizeof(_batch))
        {
            std::memcpy(_batch + _batchLen, row, std::strlen(row));
            _batchLen += std::strlen(row);
        }
        _markerWritten = true;
    }

    // --- the four named failure conditions --------------------------------------
    // Shared path for short write / write error / removed / full. Each logs once,
    // flags the OLED via ipc.sd, and returns - the loop keeps draining log_q.
    void handleFailure()
    {
        if (_file)
        {
            std::fclose(_file);
            _file = nullptr;
        }
        _dirty = false;

        uint64_t total = 0, freeB = 0;
        const bool haveFree = _sd.freeSpace(total, freeB);
        if (haveFree && freeB < FREE_LOW)
        {
            _full = true;
            _ipc.sd.full      = 1;
            _ipc.sd.freeBytes = freeB;
            sdCondition("full", 1); // card is fine, just full - not a write error
            return;
        }

        _ipc.sd.writeErrors++;

        // write error or card yanked: one remount attempt, then degrade.
        _sd.unmount();
        if (_sd.mount() == ESP_OK && openLog())
        {
            _mounted = true;
            _ipc.sd.present = 1;
            _ipc.sd.mounted = 1;
            ESP_LOGW(TAG, "recovered after write error (remount)");
            return;
        }
        _sd.unmount();
        _mounted = false;
        _ipc.sd.present = 0;
        _ipc.sd.mounted = 0;
        _lastMountTryUs = (uint64_t)esp_timer_get_time();
        sdCondition("removed", 0);
    }

    // One ESP_LOGW + one sealed `evt e:"sd"` line per distinct condition, re-armed
    // when the condition changes (docs/protocol.md §3.4).
    void sdCondition(const char *cond, uint8_t present)
    {
        if (std::strcmp(_lastCond, cond) == 0)
            return;
        std::strncpy(_lastCond, cond, sizeof(_lastCond) - 1);
        _lastCond[sizeof(_lastCond) - 1] = '\0';

        ESP_LOGW(TAG, "condition: %s (present=%u)", cond, present);

        char line[96];
        std::snprintf(line, sizeof(line),
                      "{\"t\":\"evt\",\"e\":\"sd\",\"up\":%llu,\"cond\":\"%s\",\"present\":%u}",
                      (unsigned long long)esp_timer_get_time(), cond, (unsigned)present);
        ndjson::seal(line, sizeof(line));
        if (_ipc.link)
            _ipc.link->writeLine(line);
    }

    // --- telemetry housekeeping ----------------------------------------------
    void refreshTelemetry(uint64_t now)
    {
        _ipc.sd.queueDepth     = uxQueueMessagesWaiting(_ipc.logQ);
        _ipc.sd.droppedRecords = _ipc.logDropped.load(std::memory_order_relaxed);

        if (_windowStartUs == 0)
            _windowStartUs = now;
        const uint64_t elapsed = now - _windowStartUs;
        if (elapsed >= 1000000ull)
        {
            _ipc.sd.writeBytesPerS = (uint32_t)(_bytesWindow * 1000000ull / elapsed);
            _bytesWindow   = 0;
            _windowStartUs = now;
        }

        if (_mounted && now - _lastFreePollUs >= (uint64_t)config::SD_FREE_POLL_MS * 1000ull)
        {
            _lastFreePollUs = now;
            refreshFree();
        }
    }

    void refreshFree()
    {
        uint64_t total = 0, freeB = 0;
        if (_sd.freeSpace(total, freeB))
            _ipc.sd.freeBytes = freeB;
    }

    void recordLatency(uint64_t dtUs)
    {
        const uint32_t us = (dtUs > UINT32_MAX) ? UINT32_MAX : (uint32_t)dtUs;
        if (us > _ipc.sd.writeMaxLatencyUs)
            _ipc.sd.writeMaxLatencyUs = us;

        int      b    = 0;
        uint32_t edge = 64; // bucket 0: < 64 us
        while (b < LAT_BUCKETS - 1 && us >= edge)
        {
            edge <<= 1;
            ++b;
        }
        _latHist[b]++;
        _latCount++;
        _ipc.sd.writeP95LatencyUs = p95();
    }

    uint32_t p95() const
    {
        if (_latCount == 0)
            return 0;
        const uint32_t target = (uint32_t)((uint64_t)_latCount * 95 / 100);
        uint32_t acc = 0, edge = 64;
        for (int b = 0; b < LAT_BUCKETS; ++b)
        {
            acc += _latHist[b];
            if (acc >= target)
                return edge;
            edge <<= 1;
        }
        return edge;
    }

    Ipc    &_ipc;
    Sdcard &_sd;

    FILE  *_file    = nullptr;
    char   _batch[config::SD_BATCH_MAX_BYTES]; // in the task object (BSS), not the stack
    size_t _batchLen = 0;

    bool _mounted       = false;
    bool _full          = false;
    bool _dirty         = false;
    bool _markerWritten = false;
    bool _haveBootReason = false;
    char _bootReason[24] = {0};
    char _lastCond[16]   = {0};

    uint64_t _lastSyncUs     = 0;
    uint64_t _lastMountTryUs = 0;
    uint64_t _lastFreePollUs = 0;
    uint64_t _windowStartUs  = 0;
    uint64_t _bytesWindow    = 0;

    uint32_t _latHist[LAT_BUCKETS] = {0};
    uint32_t _latCount             = 0;
};
