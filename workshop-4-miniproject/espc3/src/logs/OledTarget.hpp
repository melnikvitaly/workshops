#pragma once
#include "LogsTarget.hpp"
#include "../hardware/I2cBus.hpp"
#include "../LogProtocol.hpp"
#include <driver/i2c_master.h>
#include <esp_log.h>
#include <cstdio>
#include <cstring>

// LogsTarget that shows the most recent records on a 128x64 SSD1306 OLED over
// I2C. Keeps a rolling window of the last 8 lines (one per 8-pixel page row) and
// redraws the whole screen on each record. One compact line per record:
//   "<uptime_s> OBJ v"  — slave uptime seconds, 4-char object id, value.
//
// Self-contained: it owns the SSD1306 command set, a 5x7 font, and a 1 KB frame
// buffer. Give it an initialised I2cBus and its 7-bit address.
class OledTarget : public LogsTarget
{
public:
    static constexpr int WIDTH   = 128;
    static constexpr int HEIGHT  = 64;
    static constexpr int PAGES   = HEIGHT / 8;   // 8
    static constexpr int COLS    = WIDTH / 6;    // 21 chars per row (6px cells)
    static constexpr int ROWS    = PAGES;        // 8 text rows

    OledTarget(I2cBus& bus, uint8_t addr = 0x3C, uint32_t hz = 400'000)
        : _bus(bus), _addr(addr), _hz(hz) {}

    bool init() override
    {
        if (!_bus.ready())
        {
            ESP_LOGE("OledTarget", "I2C bus not initialised");
            return false;
        }
        if (_bus.addDevice(_addr, _hz, &_dev) != ESP_OK)
        {
            ESP_LOGE("OledTarget", "device 0x%02X add failed", _addr);
            return false;
        }
        static const uint8_t initSeq[] = {
            0xAE,             // display off
            0xD5, 0x80,       // clock div
            0xA8, 0x3F,       // multiplex = 63 (64 rows)
            0xD3, 0x00,       // display offset 0
            0x40,             // start line 0
            0x8D, 0x14,       // charge pump on
            0x20, 0x00,       // horizontal addressing mode
            0xA1,             // segment remap
            0xC8,             // COM scan direction remapped
            0xDA, 0x12,       // COM pins config
            0x81, 0xCF,       // contrast
            0xD9, 0xF1,       // pre-charge
            0xDB, 0x40,       // VCOM detect
            0xA4,             // resume to RAM content
            0xA6,             // normal (non-inverted)
            0xAF,             // display on
        };
        if (!sendCommands(initSeq, sizeof(initSeq)))
        {
            ESP_LOGE("OledTarget", "init sequence failed");
            return false;
        }
        clearRows();
        flush();
        _ready = true;
        return true;
    }

    void write(const LogRecord& rec) override
    {
        if (!_ready)
            return;

        char val[24];
        formatValue(val, sizeof(val), rec);

        // Full line into a buffer that can't overflow; pushRow() clips it to the
        // screen width (COLS) when it stores the row.
        char line[48];
        snprintf(line, sizeof(line), "%us %.4s %s",
                 (unsigned)(rec.eventTimeMs / 1000), rec.objectId, val);

        pushRow(line);
        render();
        flush();
    }

    const char* name() const override { return "oled"; }

private:
    I2cBus&                 _bus;
    uint8_t                 _addr;
    uint32_t                _hz;
    i2c_master_dev_handle_t _dev   = nullptr;
    bool                    _ready = false;

    char    _rows[ROWS][COLS + 1] = {};   // rolling text lines
    int     _rowCount             = 0;    // filled rows (grows then scrolls)
    uint8_t _fb[WIDTH * PAGES]    = {};    // frame buffer, page-major

    // --- I2C helpers ---------------------------------------------------------
    // SSD1306 control byte 0x00 => the bytes that follow are commands.
    bool sendCommands(const uint8_t* cmds, size_t n)
    {
        uint8_t buf[40];
        if (n + 1 > sizeof(buf))
            return false;
        buf[0] = 0x00;
        memcpy(buf + 1, cmds, n);
        return i2c_master_transmit(_dev, buf, n + 1, 100) == ESP_OK;
    }

    // Push the whole frame buffer; control byte 0x40 => data (GDDRAM).
    void flush()
    {
        static const uint8_t window[] = {
            0x21, 0x00, WIDTH - 1,   // column range 0..127
            0x22, 0x00, PAGES - 1,   // page range 0..7
        };
        if (!sendCommands(window, sizeof(window)))
            return;
        for (int p = 0; p < PAGES; ++p)
        {
            uint8_t buf[1 + WIDTH];
            buf[0] = 0x40;
            memcpy(buf + 1, &_fb[p * WIDTH], WIDTH);
            if (i2c_master_transmit(_dev, buf, sizeof(buf), 100) != ESP_OK)
                return;
        }
    }

    // --- text / rendering ----------------------------------------------------
    void clearRows()
    {
        memset(_rows, 0, sizeof(_rows));
        _rowCount = 0;
    }

    // Append a line, scrolling the window up once it is full.
    void pushRow(const char* line)
    {
        if (_rowCount < ROWS)
        {
            snprintf(_rows[_rowCount++], COLS + 1, "%s", line);
            return;
        }
        for (int r = 1; r < ROWS; ++r)
            memcpy(_rows[r - 1], _rows[r], COLS + 1);
        snprintf(_rows[ROWS - 1], COLS + 1, "%s", line);
    }

    // Draw all rows into the frame buffer.
    void render()
    {
        memset(_fb, 0, sizeof(_fb));
        for (int r = 0; r < _rowCount; ++r)
            for (int c = 0; c < COLS && _rows[r][c]; ++c)
                drawGlyph(c * 6, r, _rows[r][c]);
    }

    // Blit one 5x7 glyph at pixel-x, page-row (each row is one 8px page).
    void drawGlyph(int x, int page, char ch)
    {
        const uint8_t* g = glyph(ch);
        uint8_t* col = &_fb[page * WIDTH + x];
        for (int i = 0; i < 5 && x + i < WIDTH; ++i)
            col[i] = g[i];
        // 6th column is the inter-character gap (already 0).
    }

    static const uint8_t* glyph(char ch)
    {
        static const uint8_t blank[5] = {0, 0, 0, 0, 0};
        uint8_t u = (uint8_t)ch;
        if (u < 0x20 || u > 0x7E)
            return blank;
        return FONT5x7[u - 0x20];
    }

    // Render the typed value into `out` (compact form for the small screen).
    static void formatValue(char* out, size_t cap, const LogRecord& rec)
    {
        const uint8_t* d = rec.data;
        auto le = [&](int nbytes) -> uint32_t {
            uint32_t v = 0;
            for (int i = 0; i < nbytes && i < rec.len; ++i)
                v |= (uint32_t)d[i] << (8 * i);
            return v;
        };
        switch (rec.valueType)
        {
        case logproto::VT_U8:  snprintf(out, cap, "%u", (unsigned)le(1)); break;
        case logproto::VT_U16: snprintf(out, cap, "%u", (unsigned)le(2)); break;
        case logproto::VT_U32: snprintf(out, cap, "%u", (unsigned)le(4)); break;
        case logproto::VT_I8:  snprintf(out, cap, "%d", (int)(int8_t)le(1)); break;
        case logproto::VT_I16: snprintf(out, cap, "%d", (int)(int16_t)le(2)); break;
        case logproto::VT_I32: snprintf(out, cap, "%d", (int)(int32_t)le(4)); break;
        case logproto::VT_F32:
        {
            uint32_t bits = le(4); float f; memcpy(&f, &bits, sizeof(f));
            snprintf(out, cap, "%.2f", f); break;
        }
        case logproto::VT_DATETIME:
            if (rec.len >= 6)
                snprintf(out, cap, "%02u-%02u %02u:%02u", d[1], d[2], d[3], d[4]);
            else
                snprintf(out, cap, "?");
            break;
        case logproto::VT_STR:
        default:
            snprintf(out, cap, "%.*s", (int)rec.len, (const char*)d); break;
        }
    }

    // 5x7 font, columns (LSB = top pixel), ASCII 0x20..0x7E.
    static const uint8_t FONT5x7[95][5];
};

inline const uint8_t OledTarget::FONT5x7[95][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x00,0x00,0x5F,0x00,0x00}, // !
    {0x00,0x07,0x00,0x07,0x00}, // "
    {0x14,0x7F,0x14,0x7F,0x14}, // #
    {0x24,0x2A,0x7F,0x2A,0x12}, // $
    {0x23,0x13,0x08,0x64,0x62}, // %
    {0x36,0x49,0x55,0x22,0x50}, // &
    {0x00,0x05,0x03,0x00,0x00}, // '
    {0x00,0x1C,0x22,0x41,0x00}, // (
    {0x00,0x41,0x22,0x1C,0x00}, // )
    {0x14,0x08,0x3E,0x08,0x14}, // *
    {0x08,0x08,0x3E,0x08,0x08}, // +
    {0x00,0x50,0x30,0x00,0x00}, // ,
    {0x08,0x08,0x08,0x08,0x08}, // -
    {0x00,0x60,0x60,0x00,0x00}, // .
    {0x20,0x10,0x08,0x04,0x02}, // /
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
    {0x00,0x36,0x36,0x00,0x00}, // :
    {0x00,0x56,0x36,0x00,0x00}, // ;
    {0x08,0x14,0x22,0x41,0x00}, // <
    {0x14,0x14,0x14,0x14,0x14}, // =
    {0x00,0x41,0x22,0x14,0x08}, // >
    {0x02,0x01,0x51,0x09,0x06}, // ?
    {0x32,0x49,0x79,0x41,0x3E}, // @
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x7F,0x20,0x18,0x20,0x7F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
    {0x00,0x7F,0x41,0x41,0x00}, // [
    {0x02,0x04,0x08,0x10,0x20}, // backslash
    {0x00,0x41,0x41,0x7F,0x00}, // ]
    {0x04,0x02,0x01,0x02,0x04}, // ^
    {0x40,0x40,0x40,0x40,0x40}, // _
    {0x00,0x01,0x02,0x04,0x00}, // `
    {0x20,0x54,0x54,0x54,0x78}, // a
    {0x7F,0x48,0x44,0x44,0x38}, // b
    {0x38,0x44,0x44,0x44,0x20}, // c
    {0x38,0x44,0x44,0x48,0x7F}, // d
    {0x38,0x54,0x54,0x54,0x18}, // e
    {0x08,0x7E,0x09,0x01,0x02}, // f
    {0x0C,0x52,0x52,0x52,0x3E}, // g
    {0x7F,0x08,0x04,0x04,0x78}, // h
    {0x00,0x44,0x7D,0x40,0x00}, // i
    {0x20,0x40,0x44,0x3D,0x00}, // j
    {0x7F,0x10,0x28,0x44,0x00}, // k
    {0x00,0x41,0x7F,0x40,0x00}, // l
    {0x7C,0x04,0x18,0x04,0x78}, // m
    {0x7C,0x08,0x04,0x04,0x78}, // n
    {0x38,0x44,0x44,0x44,0x38}, // o
    {0x7C,0x14,0x14,0x14,0x08}, // p
    {0x08,0x14,0x14,0x18,0x7C}, // q
    {0x7C,0x08,0x04,0x04,0x08}, // r
    {0x48,0x54,0x54,0x54,0x20}, // s
    {0x04,0x3F,0x44,0x40,0x20}, // t
    {0x3C,0x40,0x40,0x20,0x7C}, // u
    {0x1C,0x20,0x40,0x20,0x1C}, // v
    {0x3C,0x40,0x30,0x40,0x3C}, // w
    {0x44,0x28,0x10,0x28,0x44}, // x
    {0x0C,0x50,0x50,0x50,0x3C}, // y
    {0x44,0x64,0x54,0x4C,0x44}, // z
    {0x00,0x08,0x36,0x41,0x00}, // {
    {0x00,0x00,0x7F,0x00,0x00}, // |
    {0x00,0x41,0x36,0x08,0x00}, // }
    {0x08,0x04,0x08,0x10,0x08}, // ~
};
