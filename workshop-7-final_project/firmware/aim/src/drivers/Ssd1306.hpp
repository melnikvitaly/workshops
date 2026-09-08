#pragma once
#include <cstdint>
#include <cstring>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include "Pinout.hpp"
#include "Font5x7.hpp"

// SSD1306 128x64 OLED on I2C0 (docs/interfaces.md §3), IDF 6 i2c_master driver.
//
// One device on the bus, so a NAK is unambiguous: it means the display. Every
// call returns an esp_err_t; the ui task logs the first failure once, disables
// itself and keeps polling the buttons - a missing display is a lost
// convenience, never a lost gimbal.
class Ssd1306
{
public:
    static constexpr uint8_t WIDTH   = 128;
    static constexpr uint8_t PAGES   = 8;   // 64 px / 8
    static constexpr uint8_t COLS    = 21;  // 128 / 6
    static constexpr uint8_t CHAR_W  = 6;

    esp_err_t init()
    {
        i2c_master_bus_config_t busCfg = {};
        busCfg.i2c_port                     = pinout::OLED_I2C_PORT;
        busCfg.sda_io_num                   = pinout::OLED_SDA;
        busCfg.scl_io_num                   = pinout::OLED_SCL;
        busCfg.clk_source                   = I2C_CLK_SRC_DEFAULT;
        busCfg.glitch_ignore_cnt            = 7;
        busCfg.flags.enable_internal_pullup = true;

        esp_err_t e = i2c_new_master_bus(&busCfg, &_bus);
        if (e != ESP_OK)
            return e;

        i2c_device_config_t devCfg = {};
        devCfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        devCfg.device_address  = pinout::OLED_ADDR;
        devCfg.scl_speed_hz    = pinout::OLED_HZ;

        e = i2c_master_bus_add_device(_bus, &devCfg, &_dev);
        if (e != ESP_OK)
            return e;

        e = i2c_master_probe(_bus, pinout::OLED_ADDR, pdMS_TO_TICKS(50));
        if (e != ESP_OK)
            return e;

        static const uint8_t initSeq[] = {
            0xAE,             // display off
            0x20, 0x00,       // horizontal addressing mode
            0xB0, 0xC8,       // page start, COM scan dec
            0x00, 0x10,       // low/high column
            0x40,             // start line 0
            0x81, 0x7F,       // contrast
            0xA1,             // segment remap
            0xA6,             // normal (not inverted)
            0xA8, 0x3F,       // multiplex 1/64
            0xD3, 0x00,       // display offset 0
            0xD5, 0x80,       // clock divide
            0xD9, 0xF1,       // pre-charge
            0xDA, 0x12,       // COM pins
            0xDB, 0x40,       // vcom detect
            0x8D, 0x14,       // charge pump on
            0xAF,             // display on
        };
        for (uint8_t c : initSeq)
        {
            e = cmd(c);
            if (e != ESP_OK)
                return e;
        }

        clear();
        return flush();
    }

    void clear() { std::memset(_fb, 0x00, sizeof(_fb)); }

    // Draw a string at (col, page). col is a pixel column, page is an 8-px row
    // band (0..7). Clipped to the panel.
    void text(uint8_t col, uint8_t page, const char *s)
    {
        if (page >= PAGES)
            return;
        uint8_t x = col;
        for (; *s && x + CHAR_W <= WIDTH; ++s, x += CHAR_W)
        {
            const uint8_t *g = font5x7::glyph(*s);
            for (uint8_t i = 0; i < font5x7::WIDTH; ++i)
                _fb[page * WIDTH + x + i] = g[i];
            _fb[page * WIDTH + x + font5x7::WIDTH] = 0x00; // spacer column
        }
    }

    esp_err_t flush()
    {
        _tx[0] = 0x40; // Co=0, D/C=1 -> data stream
        std::memcpy(&_tx[1], _fb, sizeof(_fb));
        return i2c_master_transmit(_dev, _tx, sizeof(_tx), pdMS_TO_TICKS(50));
    }

private:
    esp_err_t cmd(uint8_t c)
    {
        const uint8_t buf[2] = {0x00, c}; // Co=0, D/C=0 -> command
        return i2c_master_transmit(_dev, buf, sizeof(buf), pdMS_TO_TICKS(50));
    }

    i2c_master_bus_handle_t _bus = nullptr;
    i2c_master_dev_handle_t _dev = nullptr;

    uint8_t _fb[WIDTH * PAGES];
    uint8_t _tx[1 + WIDTH * PAGES];
};
