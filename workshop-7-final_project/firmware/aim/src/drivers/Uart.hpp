#pragma once
#include <driver/uart.h>
#include <cstring>
#include <cstddef>
#include <cstdint>

// Non-blocking, line-oriented UART reader.
//
// Sharing UART0 with the console
// ------------------------------
// This project receives its error-vector stream on the same port the ESP-IDF
// console logs to (CONFIG_ESP_CONSOLE_UART_NUM = 0, GPIO43/44). That works:
// installing the driver only adds an RX path, while ESP_LOG keeps writing
// through the console's own TX path. The visible cost is that your terminal
// shows log output interleaved with whatever the PC is sending.
//
// On UART0 the hardware is deliberately left alone - no uart_param_config(),
// no uart_set_pin() - because the console has already configured baud, framing
// and pins. Touching them would disturb logging for no benefit.
template <int MaxLine = 96>
class Uart
{
    uart_port_t _port;
    int         _baud;
    int         _rxBufBytes;

    char   _line[MaxLine];
    size_t _len      = 0;
    bool   _overflow = false; // current line too long -> drop through to newline

public:
    explicit Uart(uart_port_t port = UART_NUM_0, int baud = 115200, int rxBufBytes = 512)
        : _port(port), _baud(baud), _rxBufBytes(rxBufBytes) {}

    void init()
    {
        if (uart_is_driver_installed(_port))
            return;

        // Only configure the hardware on a port the console does not own.
        if (_port != UART_NUM_0)
        {
            uart_config_t cfg = {};
            cfg.baud_rate  = _baud;
            cfg.data_bits  = UART_DATA_8_BITS;
            cfg.parity     = UART_PARITY_DISABLE;
            cfg.stop_bits  = UART_STOP_BITS_1;
            cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
            cfg.source_clk = UART_SCLK_DEFAULT;
            ESP_ERROR_CHECK(uart_param_config(_port, &cfg));
        }

        // RX buffer only (tx_buffer_size = 0 -> blocking writes, which we never
        // use); no event queue, since we poll from the main loop.
        ESP_ERROR_CHECK(uart_driver_install(_port, _rxBufBytes, 0, 0, nullptr, 0));
    }

    // Hand back one complete line per call, without ever blocking. Returns
    // false when no full line has arrived yet, so call it in a loop until it
    // does - several frames can be waiting in the buffer at once.
    //
    // Empty lines and over-long lines are swallowed rather than reported: a
    // truncated frame is worse than a missing one, because acting on half a
    // coordinate pair would move the gimbal somewhere arbitrary.
    bool readLine(char *out, size_t cap)
    {
        uint8_t ch;
        while (uart_read_bytes(_port, &ch, 1, 0) == 1)
        {
            if (ch == '\r')
                continue;

            if (ch != '\n')
            {
                if (_len + 1 < MaxLine)
                    _line[_len++] = (char)ch;
                else
                    _overflow = true;
                continue;
            }

            const bool usable = !_overflow && _len > 0;
            _line[_len] = '\0';
            _len        = 0;
            _overflow   = false;

            if (usable && cap > 0)
            {
                std::strncpy(out, _line, cap - 1);
                out[cap - 1] = '\0';
                return true;
            }
        }
        return false;
    }
};
