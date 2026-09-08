#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>

#include "Ipc.hpp"
#include "CmdQueue.hpp"
#include "StateMachine.hpp"

// The logger task (docs/architecture.md §2). Lowest priority, the only task
// allowed a long block. It drains log_q; for task #2 the sink is the console.
//
// Task #4 replaces the sink with esp_vfs_fat_sdspi_mount, 4-8 KB batched writes
// aligned to the card block size, f_sync every N seconds, the four failure
// conditions and the sd.* health/throughput telemetry. The drop-oldest queue
// and its counter (ipc.logDropped) are already real - see logSend().
class LoggerTask
{
public:
    explicit LoggerTask(Ipc &ipc) : _ipc(ipc) {}

    static void entry(void *arg) { static_cast<LoggerTask *>(arg)->run(); }

    void run()
    {
        ESP_LOGI(TAG, "logger up - console sink (SD sink lands in task #4)");
        ESP_LOGI(TAG, "CSV: seq,t_mono_us,t_wall_iso,state,channel,ex,ey,vpan,vtilt,pan,tilt,flags");

        LogRecord r;
        for (;;)
        {
            if (xQueueReceive(_ipc.logQ, &r, portMAX_DELAY) != pdTRUE)
                continue;

            switch (r.kind)
            {
            case LogRecord::Kind::Boot:
                ESP_LOGI(TAG, "BOOT seq=%lu reason=%s", (unsigned long)r.seq, r.note);
                break;

            case LogRecord::Kind::Transition:
                ESP_LOGI(TAG, "EVT -> %s (%s)", stateName(r.state), r.note);
                break;

            case LogRecord::Kind::Sd:
                ESP_LOGW(TAG, "SD %s", r.note);
                break;

            case LogRecord::Kind::Sample:
                // t_wall_iso stays empty in Phase 0 - monotonic time only.
                ESP_LOGD(TAG,
                         "%lu,%llu,,%s,%u,%.3f,%.3f,%.2f,%.2f,%.1f,%.1f,%lu",
                         (unsigned long)r.seq, (unsigned long long)r.t_mono_us,
                         stateName(r.state), r.channel,
                         (double)r.ex, (double)r.ey, (double)r.vpan, (double)r.vtilt,
                         (double)r.pan, (double)r.tilt, (unsigned long)r.flags);
                break;
            }
        }
    }

private:
    static constexpr char TAG[] = "LOG";
    Ipc &_ipc;
};
