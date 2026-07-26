#pragma once
#include "LogsTarget.hpp"
#include "../LogProtocol.hpp"
#include <driver/uart.h>
#include <esp_log.h>
#include <cstdio>
#include <cstring>

// LogsTarget that streams each record as a text line out of a UART port.
//
// Defaults to a dedicated port/pin (UART1 on GPIO0) so it does not fight the
// USB console on UART0; point a USB-serial adapter at the TX pin to watch it.
// Set port to UART_NUM_0 to reuse the console instead.
class UartTarget : public LogsTarget
{
    uart_port_t _port;
    gpio_num_t  _tx;
    int         _baud;
    bool        _ready = false;

public:
    explicit UartTarget(uart_port_t port = UART_NUM_1,
                        gpio_num_t txPin  = GPIO_NUM_0,
                        int baud          = 115200)
        : _port(port), _tx(txPin), _baud(baud) {}

    bool init() override
    {
        uart_config_t cfg = {};
        cfg.baud_rate = _baud;
        cfg.data_bits = UART_DATA_8_BITS;
        cfg.parity    = UART_PARITY_DISABLE;
        cfg.stop_bits = UART_STOP_BITS_1;
        cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
        cfg.source_clk = UART_SCLK_DEFAULT;

        esp_err_t r = uart_param_config(_port, &cfg);
        if (r == ESP_OK)
            r = uart_set_pin(_port, _tx, UART_PIN_NO_CHANGE,
                             UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        // TX-only sink: no RX buffer, no event queue.
        if (r == ESP_OK && !uart_is_driver_installed(_port))
            r = uart_driver_install(_port, UART_HW_FIFO_LEN(_port) + 1, 0, 0, nullptr, 0);

        _ready = (r == ESP_OK);
        if (!_ready)
            ESP_LOGE("UartTarget", "init failed: %s", esp_err_to_name(r));
        return _ready;
    }

    void write(const LogRecord& rec) override
    {
        if (!_ready)
            return;

        // "[up=<ms> col=<us>] CS<pin> #<id> <objId>=<value>\r\n" — up= is the
        // slave's raw uptime stamp, col= is the master's own collection time
        // (esp_timer, us since boot); the value is rendered per its ValueType.
        char line[128];
        int n = snprintf(line, sizeof(line), "[up=%u col=%lld] CS%d #%u %.4s=",
                         (unsigned)rec.eventTimeMs, (long long)rec.collectedAtUs,
                         (int)rec.cs, (unsigned)rec.seq, rec.objectId);
        n += formatLogValue(line + n, sizeof(line) - n, rec);
        if (n < 0)
            return;
        if (n > (int)sizeof(line))
            n = sizeof(line);
        uart_write_bytes(_port, line, n);
        uart_write_bytes(_port, "\r\n", 2);
    }

    const char* name() const override { return "uart"; }
};
