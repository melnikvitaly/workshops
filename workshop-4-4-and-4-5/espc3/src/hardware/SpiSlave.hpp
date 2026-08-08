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

        // SPI_DMA_CH_AUTO, and this is NOT a free choice — SPI_DMA_DISABLED
        // does not work on this chip. Measured: with DMA disabled the very
        // first frame after boot decodes perfectly and every single one after
        // it comes back as a garbage, non-byte-aligned fragment (3 bits, 41
        // bits, 6 bits...). The reason is in the driver, not on the wire.
        //
        // spi_slave_queue_trans() does not arm the hardware; it only queues the
        // transaction and calls esp_intr_enable(). Arming happens exclusively
        // inside the driver's ISR, and that path branches on DMA
        // (spi_slave.c, spi_intr):
        //
        //     spi_slave_hal_hw_reset(hal);
        //     s_spi_slave_prepare_data(host);   // no-DMA: fifo_reset(tx, !rx)
        //     if (use_dma) restore_cs(host);    // "Only connect the CS ...
        //                                       //  when slave is ready"
        //     spi_slave_hal_user_start(hal);
        //
        // So the first transaction — armed by spi_slave_initialize() on
        // freshly reset hardware — is clean, and every re-arm after it goes
        // through the branch that never resets the RX FIFO and gets none of the
        // CS protection. That is exactly the observed one-good-frame-then-junk.
        //
        // On the workshop's "no circular DMA transferring or async
        // transmissions" rule: this stays within it. There is no circular
        // buffer and nothing free-running — one transaction exists at a time,
        // receive() blocks until that transaction completes, and the frame
        // still maps one-to-one onto one CS assertion. DMA here is only how the
        // driver moves 32 bytes out of the peripheral during a call we are
        // already waiting inside; the master end has no DMA at all.
        esp_err_t r = spi_slave_initialize(_host, &bus, &slave, SPI_DMA_CH_AUTO);
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

    // Wait for the master to clock one frame in, and report how many **bits**
    // it actually sent.
    //
    // `spi_slave_transmit()` is the synchronous one-call API — internally it is
    // `spi_slave_queue_trans()` followed by `spi_slave_get_trans_result()`, and
    // the driver source carries a `//ToDo: check if any spi transfers in
    // flight` right above it. That ToDo is why the timeout here is
    // `portMAX_DELAY` and not a finite value: on a timeout the queued
    // transaction stays armed in the hardware, so a caller that looped with a
    // finite timeout would queue a second, third, fourth transaction behind it
    // for as long as the link was quiet. Blocking forever means exactly one
    // transaction exists at any moment. (A "link quiet" notice therefore lives
    // on a timer in main.cpp, off this path entirely.)
    //
    // Arming is the real subtlety of an SPI slave: the hardware receives into a
    // buffer the driver was handed *in advance*, so a frame that arrives while
    // nothing is armed is not captured at all — it leaves no trace but a gap in
    // the sequence numbers at the far end.
    //
    // Bits, not bytes, on purpose: a count that is not a whole number of bytes
    // says the transaction ended mid-byte, which is a CS/clock fault, while a
    // clean but short byte count says it ended early. Rounding to bytes throws
    // that distinction away — and it is precisely the distinction that
    // identified the no-DMA re-arming bug documented in init().
    //
    // `rx` must be DMA-capable, word-aligned (DMA_ATTR) and hold at least `len`
    // bytes, and must stay alive across calls — the driver writes into it while
    // this function is not running.
    esp_err_t receive(uint8_t* rx, size_t len, size_t& receivedBits)
    {
        receivedBits = 0;

        // Clear first: a short transaction leaves the untouched tail of the
        // buffer holding the previous frame's bytes, which could still pass
        // magic and CRC and read as a frame that never arrived.
        std::memset(rx, 0, len);

        spi_slave_transaction_t t = {};
        t.length    = len * 8;   // bits, and it is the maximum accepted
        t.rx_buffer = rx;
        t.tx_buffer = nullptr;   // one-way link: we never answer the master

        esp_err_t r = spi_slave_transmit(_host, &t, portMAX_DELAY);
        if (r != ESP_OK)
            return r;

        receivedBits = t.trans_len;
        return ESP_OK;
    }
};
