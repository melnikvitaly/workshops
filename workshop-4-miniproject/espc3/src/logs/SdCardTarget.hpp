#pragma once
#include "LogsTarget.hpp"
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <esp_log.h>
#include <cstdio>
#include <unistd.h>
#include <cerrno>
#include <cstring>

// LogsTarget that appends every record as a text line to a file on an SD
// card. The card rides the shared SPI bus (its own CS pin, same MOSI/MISO/
// SCLK as the log slaves) — init() attaches it and mounts FAT on top of a bus
// the caller must have already spi_bus_initialize()'d (see SpiBus::init()),
// it does not initialize the bus itself.
//
// write() appends synchronously, inline on the collector's own task: every
// record does fprintf + fflush + fsync before returning, so a slow SD write
// directly stalls collection. That's the simplest thing that can't silently
// lose data, and is fine while record rate is low; move to a queue + separate
// writer task if drain latency ever suffers for it.
//
// fflush() alone is not enough: it only pushes the C library's stdio buffer
// down to the underlying write(), which reaches FatFs's f_write() and does
// land the bytes in allocated clusters — but FatFs doesn't rewrite the
// directory entry (the field holding the file's size) until f_sync()/
// f_close(). Without fsync(), a card pulled without a clean unmount reads
// back as a 0-byte file on a PC even though every record was actually
// written to it.
class SdCardTarget : public LogsTarget
{
public:
    SdCardTarget(spi_host_device_t host, gpio_num_t cs,
                 const char* mountPoint = "/sdcard",
                 const char* fileName   = "datalog.txt")
        : _host(host), _cs(cs), _mountPoint(mountPoint), _fileName(fileName) {}

    bool init() override
    {
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = _host;

        sdspi_device_config_t slotCfg = SDSPI_DEVICE_CONFIG_DEFAULT();
        slotCfg.host_id = _host;
        slotCfg.gpio_cs = _cs;

        esp_vfs_fat_mount_config_t mountCfg = {};
        mountCfg.format_if_mount_failed = false;
        mountCfg.max_files              = 4;
        mountCfg.allocation_unit_size   = 16 * 1024;

        esp_err_t r = esp_vfs_fat_sdspi_mount(_mountPoint, &host, &slotCfg, &mountCfg, &_card);
        if (r != ESP_OK)
        {
            ESP_LOGE(TAG, "mount failed on CS%d: %s", (int)_cs, esp_err_to_name(r));
            return false;
        }

        char path[64];
        snprintf(path, sizeof(path), "%s/%s", _mountPoint, _fileName);
        _file = fopen(path, "a");
        if (!_file)
        {
            ESP_LOGE(TAG, "could not open %s for append", path);
            return false;
        }
        ESP_LOGI(TAG, "appending records to %s", path);
        return true;
    }

    void write(const LogRecord& rec) override
    {
        if (!_file)
            return;

        // Same line shape as UartTarget, so the two are diffable by eye.
        char val[48];
        formatLogValue(val, sizeof(val), rec);
        errno = 0;
        int n = fprintf(_file, "[up=%u col=%lld] CS%d #%u %.4s=%s\n",
                        (unsigned)rec.eventTimeMs, (long long)rec.collectedAtUs,
                        (int)rec.cs, (unsigned)rec.seq, rec.objectId, val);
        if (n < 0)
        {
            ESP_LOGE(TAG, "fprintf failed: %s", strerror(errno));
            return;
        }

        errno = 0;
        if (fflush(_file) != 0)
        {
            ESP_LOGE(TAG, "fflush failed: %s", strerror(errno));
            return;
        }

        errno = 0;
        if (fsync(fileno(_file)) != 0)
        {
            ESP_LOGE(TAG, "fsync failed: %s", strerror(errno));
            return;
        }
    }

    const char* name() const override { return "sdcard"; }

private:
    static constexpr const char* TAG = "SdCardTarget";
    spi_host_device_t _host;
    gpio_num_t         _cs;
    const char*        _mountPoint;
    const char*        _fileName;
    sdmmc_card_t*      _card = nullptr;
    FILE*              _file = nullptr;
};
