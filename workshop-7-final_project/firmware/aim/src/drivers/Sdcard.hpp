#pragma once
#include <cstdint>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include "Pinout.hpp"
#include "Config.hpp"

// micro-SD over SPI2 / FSPI (docs/interfaces.md §4). This class owns both the
// SPI bus and the FAT mount - SPI2 carries nothing else in Phase 0.
//
// Nothing here calls ESP_ERROR_CHECK: a mount or write failure is one of the
// four named conditions the system is designed to survive (no card, removed,
// full, write error), and an abort() would reboot a tracking gimbal. The caller
// (logger) counts the failure and keeps running.
//
// The SPI bus is initialised with SPI_DMA_CH_AUTO, so sdspi block transfers are
// DMA-backed on ESP-IDF 6.x (requirement 6.2).
class Sdcard
{
public:
    // Bring up the SPI bus (once) and mount the FAT volume. Safe to call again
    // after unmount() for a remount - the bus stays up between attempts.
    esp_err_t mount()
    {
        if (!_busReady)
        {
            spi_bus_config_t busCfg = {};
            busCfg.mosi_io_num     = pinout::SD_MOSI;
            busCfg.miso_io_num     = pinout::SD_MISO;
            busCfg.sclk_io_num     = pinout::SD_SCK;
            busCfg.quadwp_io_num   = -1;
            busCfg.quadhd_io_num   = -1;
            busCfg.max_transfer_sz = config::SD_BATCH_MAX_BYTES;

            const esp_err_t e = spi_bus_initialize(HOST, &busCfg, SPI_DMA_CH_AUTO);
            if (e != ESP_OK && e != ESP_ERR_INVALID_STATE) // INVALID_STATE: already up
                return e;
            _busReady = true;
            ESP_LOGI(TAG, "SPI2 bus up (SPI_DMA_CH_AUTO) - sdspi transfers are DMA-backed");
        }

        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot         = HOST;
        host.max_freq_khz = SDMMC_FREQ_DEFAULT; // 20 MHz; 400 kHz init ramp is automatic

        sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot.host_id = HOST;
        slot.gpio_cs = pinout::SD_CS;

        esp_vfs_fat_mount_config_t mountCfg = {};
        mountCfg.format_if_mount_failed = false;
        mountCfg.max_files              = 2; // docs/interfaces.md §4
        mountCfg.allocation_unit_size   = 16 * 1024;

        const esp_err_t e =
            esp_vfs_fat_sdspi_mount(config::SD_MOUNT_POINT, &host, &slot, &mountCfg, &_card);
        if (e != ESP_OK)
        {
            _card = nullptr;
            ESP_LOGW(TAG, "mount failed: %s", esp_err_to_name(e));
            return e;
        }
        ESP_LOGI(TAG, "mounted %s (%llu MB)", config::SD_MOUNT_POINT,
                 ((uint64_t)_card->csd.capacity * _card->csd.sector_size) >> 20);
        return ESP_OK;
    }

    void unmount()
    {
        if (_card)
            esp_vfs_fat_sdcard_unmount(config::SD_MOUNT_POINT, _card);
        _card = nullptr;
    }

    // Total / free bytes on the mounted volume. False if the query fails.
    bool freeSpace(uint64_t &totalBytes, uint64_t &freeBytes) const
    {
        return esp_vfs_fat_info(config::SD_MOUNT_POINT, &totalBytes, &freeBytes) == ESP_OK;
    }

    bool mounted() const { return _card != nullptr; }

private:
    static constexpr char             TAG[]  = "SD";
    static constexpr spi_host_device_t HOST  = SPI2_HOST; // FSPI, IOMUX pins 10-13

    sdmmc_card_t *_card     = nullptr;
    bool          _busReady = false;
};
