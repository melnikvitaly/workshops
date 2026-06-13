#pragma once
#include <esp_adc/adc_oneshot.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_cali_scheme.h>
#include <esp_log.h>

class ADC
{
    static constexpr int DEFAULT_BITS = 12;   // ADC_BITWIDTH_DEFAULT resolution
    static constexpr int PERCENT_MAX  = 100;

    adc_unit_t _unit;
    adc_channel_t _channel;
    adc_atten_t _atten;
    adc_bitwidth_t _bitwidth;
    int _maxRaw;
    adc_oneshot_unit_handle_t _handle = nullptr;
    adc_cali_handle_t _cali = nullptr;

    static int rawMax(adc_bitwidth_t w)
    {
        int bits = (w == ADC_BITWIDTH_DEFAULT) ? DEFAULT_BITS : (int)w;
        return (1 << bits) - 1;
    }

public:
    ADC(adc_unit_t unit, adc_channel_t channel,
        adc_atten_t atten,
        adc_bitwidth_t bitwidth = ADC_BITWIDTH_DEFAULT)
        : _unit(unit), _channel(channel), _atten(atten),
          _bitwidth(bitwidth), _maxRaw(rawMax(bitwidth)) {}

    void init()
    {
        if (_handle) return;   // idempotent: safe if several owners share this ADC

        adc_oneshot_unit_init_cfg_t unitCfg = {
            .unit_id = _unit,
            .clk_src = (adc_oneshot_clk_src_t)0,
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&unitCfg, &_handle));

        adc_oneshot_chan_cfg_t chanCfg = {
            .atten = _atten,
            .bitwidth = _bitwidth,
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(_handle, _channel, &chanCfg));

        adc_cali_curve_fitting_config_t caliCfg = {
            .unit_id = _unit,
            .chan = _channel,
            .atten = _atten,
            .bitwidth = _bitwidth,
        };
        ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&caliCfg, &_cali));
        ESP_LOGI("ADC", "init ok unit=%d ch=%d bits=%d", _unit, _channel, _maxRaw + 1);
    }

    int readRaw()
    {
        int raw = 0;
        ESP_ERROR_CHECK(adc_oneshot_read(_handle, _channel, &raw));
        return raw;
    }

    int toMv(int raw)
    {
        int mv = 0;
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(_cali, raw, &mv));
        return mv;
    }

    int maxRaw() const { return _maxRaw; }

    uint8_t pct(int raw) const
    {
        return (uint8_t)((raw * PERCENT_MAX) / _maxRaw);
    }
};
