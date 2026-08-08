#pragma once
#include <driver/spi_slave.h>
#include <driver/gpio.h>
#include <esp_err.h>
#include <cstdint>
#include <cstring>

// Thin wrapper over an ESP-IDF SPI **slave** host.
//
// Follows the hardware/ style used across these workshops: the constructor
// stores the config, init() performs the ESP-IDF calls, and the class owns no
// data buffer — the caller passes its own to receive(), so buffer lifetime and
// alignment stay visible where they matter.
//
// Why the slave end and not a master here: the STM32 drives the clock and is
// the only talker, so this end never initiates anything. One frame at a time,
// blocking, no DMA — see receive() for the exact shape of that.
class SpiSlave
{
    spi_host_device_t _host;
    gpio_num_t        _mosi;
    gpio_num_t        _miso;
    gpio_num_t        _sclk;
    gpio_num_t        _cs;
    uint8_t           _mode;

    // The armed transaction. It has to outlive the call that queued it — the
    // driver keeps the pointer until the master finishes clocking — so it is a
    // member, not a local.
    spi_slave_transaction_t _trans  = {};
    bool                    _armed  = false;

public:
    SpiSlave(spi_host_device_t host,
             gpio_num_t mosi,
             gpio_num_t miso,
             gpio_num_t sclk,
             gpio_num_t cs,
             uint8_t    mode = 0)
        : _host(host), _mosi(mosi), _miso(miso), _sclk(sclk), _cs(cs), _mode(mode) {}

    esp_err_t init()
    {
        spi_bus_config_t bus = {};
        bus.mosi_io_num   = _mosi;
        bus.miso_io_num   = _miso;
        bus.sclk_io_num   = _sclk;
        bus.quadwp_io_num = -1;
        bus.quadhd_io_num = -1;

        spi_slave_interface_config_t slave = {};
        slave.spics_io_num  = _cs;
        slave.flags         = 0;
        slave.queue_size    = 1;   // exactly one frame may be in flight, ever
        slave.mode          = _mode;
        slave.post_setup_cb = nullptr;
        slave.post_trans_cb = nullptr;

        // SPI_DMA_DISABLED, deliberately. The workshop rules out DMA on this
        // link, and without it the slave works straight out of the hardware
        // FIFO — which caps one transaction at SOC_SPI_MAXIMUM_BUFFER_SIZE
        // (64 bytes on the C3). The frame is 32 bytes, so the cap costs nothing
        // and the data path loses its descriptors, its cache-alignment rules
        // and its 4-byte length rounding along with the DMA.
        esp_err_t r = spi_slave_initialize(_host, &bus, &slave, SPI_DMA_DISABLED);
        if (r != ESP_OK)
            return r;

        // The master parks CS high, but only once its own GPIO init has run.
        // In the window between the two boards powering up, this pull-up is
        // what stops a floating CS from starting a phantom transaction out of
        // noise — and a phantom transaction is worse than none, because it
        // consumes the armed slot the next real frame needed.
        gpio_set_pull_mode(_cs, GPIO_PULLUP_ONLY);
        return ESP_OK;
    }

    // Wait for the master to clock one frame in, and report how many bytes it
    // actually sent.
    //
    // Arming is the whole subtlety of an SPI slave: the hardware answers out of
    // a buffer the driver was given in advance, so a frame that arrives while
    // nothing is armed is not captured at all — it is simply gone, and the only
    // evidence is a gap in the sequence numbers at the far end. Hence the
    // two-step shape below: queue the transaction once, then wait for it. If
    // the wait times out, the transaction is *still armed* in the hardware and
    // this returns ESP_ERR_TIMEOUT without touching it, so the caller can print
    // a "link quiet" notice and come straight back to keep waiting on the very
    // same armed frame. Re-queueing on every call instead (what the one-liner
    // spi_slave_transmit does) would stack a second, third, fourth transaction
    // behind the first for as long as the link stays quiet.
    //
    // Still fully synchronous: one transaction exists at a time, the call
    // blocks until it completes, and nothing is pipelined or streamed.
    //
    // `rx` must be word-aligned (WORD_ALIGNED_ATTR) and hold at least `len`
    // bytes, and it must stay alive across calls — the driver writes into it
    // while this function is not running.
    esp_err_t receive(uint8_t* rx, size_t len, TickType_t timeoutTicks, size_t& receivedBytes)
    {
        receivedBytes = 0;

        if (!_armed)
        {
            // Clear first: a short/aborted transaction leaves the untouched
            // tail of the buffer holding the previous frame's bytes, which
            // could still pass magic and CRC and read as a frame that never
            // arrived.
            std::memset(rx, 0, len);

            _trans           = {};
            _trans.length    = len * 8;   // bits, and it is the maximum accepted
            _trans.rx_buffer = rx;
            _trans.tx_buffer = nullptr;   // one-way link: we never answer

            esp_err_t r = spi_slave_queue_trans(_host, &_trans, portMAX_DELAY);
            if (r != ESP_OK)
                return r;
            _armed = true;
        }

        spi_slave_transaction_t* done = nullptr;
        esp_err_t r = spi_slave_get_trans_result(_host, &done, timeoutTicks);
        if (r != ESP_OK)
            return r;   // ESP_ERR_TIMEOUT: still armed, nothing queued, nothing lost

        _armed = false;

        // trans_len is what the master really clocked, which is less than
        // `length` if it stopped mid-frame. The caller checks it against the
        // expected frame size rather than trusting the request.
        receivedBytes = done->trans_len / 8;
        return ESP_OK;
    }
};
