#pragma once
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <esp_err.h>

// Thin wrapper over an ESP-IDF I2C master bus (the shared SDA/SCL lines).
// Devices are added on top of it individually by 7-bit address, mirroring the
// SpiBus/SpiDevice split used for the SPI side.
class I2cBus
{
    i2c_port_num_t          _port;
    gpio_num_t              _sda;
    gpio_num_t              _scl;
    i2c_master_bus_handle_t _bus = nullptr;

public:
    I2cBus(i2c_port_num_t port, gpio_num_t sda, gpio_num_t scl)
        : _port(port), _sda(sda), _scl(scl) {}

    esp_err_t init()
    {
        i2c_master_bus_config_t cfg = {};
        cfg.i2c_port                     = _port;
        cfg.sda_io_num                   = _sda;
        cfg.scl_io_num                   = _scl;
        cfg.clk_source                   = I2C_CLK_SRC_DEFAULT;
        cfg.glitch_ignore_cnt            = 7;
        cfg.flags.enable_internal_pullup = true; // external pull-ups still advised
        return i2c_new_master_bus(&cfg, &_bus);
    }

    // Attach a device at `addr` (7-bit) clocked at `hz`; returns its handle.
    esp_err_t addDevice(uint8_t addr, uint32_t hz, i2c_master_dev_handle_t* out)
    {
        i2c_device_config_t dev = {};
        dev.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        dev.device_address  = addr;
        dev.scl_speed_hz    = hz;
        return i2c_master_bus_add_device(_bus, &dev, out);
    }

    bool ready() const { return _bus != nullptr; }
};
