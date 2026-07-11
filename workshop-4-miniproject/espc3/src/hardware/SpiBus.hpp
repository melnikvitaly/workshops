#pragma once
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_err.h>

// Thin wrapper over an ESP-IDF SPI host (the shared MOSI/MISO/CLK lines).
// Devices are attached on top of it individually via their own CS pins.
class SpiBus
{
    spi_host_device_t _host;
    gpio_num_t        _mosi;
    gpio_num_t        _miso;
    gpio_num_t        _sclk;

public:
    SpiBus(spi_host_device_t host,
           gpio_num_t mosi,
           gpio_num_t miso,
           gpio_num_t sclk)
        : _host(host), _mosi(mosi), _miso(miso), _sclk(sclk) {}

    esp_err_t init(int maxTransferSz = 4092)
    {
        spi_bus_config_t cfg = {};
        cfg.mosi_io_num     = _mosi;
        cfg.miso_io_num     = _miso;
        cfg.sclk_io_num     = _sclk;
        cfg.quadwp_io_num   = -1;
        cfg.quadhd_io_num   = -1;
        cfg.max_transfer_sz = maxTransferSz;

        esp_err_t r = spi_bus_initialize(_host, &cfg, SPI_DMA_CH_AUTO);
        if (r != ESP_OK)
            return r;

        // Hold MISO high while no slave drives it so an empty CS line reads as
        // 0xFF (never MAGIC) instead of floating noise. External pull-ups on
        // the slaves are still recommended for signal integrity.
        gpio_set_pull_mode(_miso, GPIO_PULLUP_ONLY);
        return ESP_OK;
    }

    spi_host_device_t host() const { return _host; }
};
