#pragma once
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <atomic>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "ITransport.hpp"
#include "Pinout.hpp"

// UART1 line I/O for the EYE link (docs/interfaces.md §2, docs/protocol.md §3.1).
//
// The receive buffer is 256 bytes - the protocol line cap. An over-long line is
// discarded through to the next newline and counted as `overlong`; the buffer is
// never grown, never truncated-and-parsed. Driver-level framing errors, overruns
// and breaks are drained from the event queue and counted as `uart_err`. Neither
// is ever fatal.
class UartTransport : public ITransport
{
public:
    // Wire the framing counters before init(). Both may be null (counting off).
    void setCounters(std::atomic<uint32_t> *overlong, std::atomic<uint32_t> *uartErr)
    {
        _overlongCtr = overlong;
        _uartErrCtr  = uartErr;
    }

    void init() override
    {
        uart_config_t cfg = {};
        cfg.baud_rate  = pinout::LINK_UART_BAUD;
        cfg.data_bits  = UART_DATA_8_BITS;
        cfg.parity     = UART_PARITY_DISABLE;
        cfg.stop_bits  = UART_STOP_BITS_1;
        cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
        cfg.source_clk = UART_SCLK_DEFAULT;

        ESP_ERROR_CHECK(uart_param_config(pinout::LINK_UART, &cfg));
        ESP_ERROR_CHECK(uart_set_pin(pinout::LINK_UART, pinout::LINK_TX, pinout::LINK_RX,
                                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
        ESP_ERROR_CHECK(uart_driver_install(pinout::LINK_UART,
                                            pinout::LINK_RX_BUF, pinout::LINK_TX_BUF,
                                            pinout::LINK_EVT_QUEUE, &_evtQueue, 0));

        _txMutex = xSemaphoreCreateMutexStatic(&_txMutexBuf);
    }

    bool readLine(char *out, size_t cap) override
    {
        drainEvents();

        uint8_t ch;
        while (uart_read_bytes(pinout::LINK_UART, &ch, 1, pdMS_TO_TICKS(READ_TIMEOUT_MS)) == 1)
        {
            if (ch == '\r')
                continue;

            if (ch != '\n')
            {
                if (_len + 1 < LINE_CAP)
                    _line[_len++] = (char)ch;
                else
                    _overflow = true; // keep consuming to the newline, never grow
                continue;
            }

            const bool usable = !_overflow && _len > 0;
            _line[_len] = '\0';
            const size_t n = _len;
            _len = 0;
            if (_overflow && _overlongCtr)
                _overlongCtr->fetch_add(1, std::memory_order_relaxed);
            _overflow = false;

            if (usable && cap > 0)
            {
                const size_t copy = (n < cap - 1) ? n : cap - 1;
                std::memcpy(out, _line, copy);
                out[copy] = '\0';
                return true;
            }
        }
        return false;
    }

    void writeLine(const char *line) override
    {
        xSemaphoreTake(_txMutex, portMAX_DELAY);
        uart_write_bytes(pinout::LINK_UART, line, std::strlen(line));
        uart_write_bytes(pinout::LINK_UART, "\n", 1);
        xSemaphoreGive(_txMutex);
    }

private:
    static constexpr int    READ_TIMEOUT_MS = 10;
    static constexpr size_t LINE_CAP        = 256; // docs/protocol.md §3.1

    // Count driver framing errors / overruns / breaks; recover the RX path on an
    // overflow so a burst of noise cannot wedge the link.
    void drainEvents()
    {
        if (!_evtQueue)
            return;
        uart_event_t ev;
        while (xQueueReceive(_evtQueue, &ev, 0) == pdTRUE)
        {
            switch (ev.type)
            {
            case UART_FIFO_OVF:
            case UART_BUFFER_FULL:
                uart_flush_input(pinout::LINK_UART);
                xQueueReset(_evtQueue);
                bumpUartErr();
                _len = 0;
                _overflow = false;
                return;
            case UART_BREAK:
            case UART_PARITY_ERR:
            case UART_FRAME_ERR:
                bumpUartErr();
                break;
            default:
                break;
            }
        }
    }

    void bumpUartErr()
    {
        if (_uartErrCtr)
            _uartErrCtr->fetch_add(1, std::memory_order_relaxed);
    }

    char   _line[LINE_CAP];
    size_t _len      = 0;
    bool   _overflow = false;

    QueueHandle_t _evtQueue = nullptr;

    std::atomic<uint32_t> *_overlongCtr = nullptr;
    std::atomic<uint32_t> *_uartErrCtr  = nullptr;

    SemaphoreHandle_t _txMutex = nullptr;
    StaticSemaphore_t _txMutexBuf{};
};
