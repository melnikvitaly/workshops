#pragma once
#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <cstring>
#include <cstdint>

// One slave sitting on a specific CS pin. attach()/detach() add and remove the
// device from the SPI host at runtime, which is what lets the collector treat
// physical plug/unplug on a predefined CS as a dynamic add/remove.
//
// Default-constructible (as an "empty slot") only so it can live in a fixed
// array inside the collector; a default-made instance must be assigned a real
// configuration before attach() is called.
class SpiDevice
{
    spi_host_device_t   _host;
    gpio_num_t          _cs;
    int                 _clockHz;
    spi_device_handle_t _handle = nullptr;

public:
    explicit SpiDevice(spi_host_device_t host = SPI2_HOST,
                       gpio_num_t cs          = GPIO_NUM_NC,
                       int clockHz            = 1'000'000)
        : _host(host), _cs(cs), _clockHz(clockHz) {}

    gpio_num_t cs() const       { return _cs; }
    bool       attached() const { return _handle != nullptr; }

    esp_err_t attach()
    {
        if (_handle)
            return ESP_OK;
        spi_device_interface_config_t dev = {};
        dev.clock_speed_hz = _clockHz;
        dev.mode           = 0;          // CPOL=0, CPHA=0
        dev.spics_io_num   = _cs;        // hardware-driven CS
        dev.queue_size     = 1;
        return spi_bus_add_device(_host, &dev, &_handle);
    }

    esp_err_t detach()
    {
        if (!_handle)
            return ESP_OK;
        esp_err_t r = spi_bus_remove_device(_handle);
        _handle = nullptr;
        return r;
    }

    // Full-duplex, fixed-length exchange. tx/rx must be DMA-capable buffers
    // (word-aligned internal RAM) of at least len bytes.
    esp_err_t transfer(const uint8_t* tx, uint8_t* rx, size_t len)
    {
        if (!_handle)
            return ESP_ERR_INVALID_STATE;
        spi_transaction_t t = {};
        t.length    = len * 8; // ESP-IDF counts transaction length in bits
        t.tx_buffer = tx;
        t.rx_buffer = rx;
        return spi_device_transmit(_handle, &t);
    }
};
