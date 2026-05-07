#pragma once
#include <Arduino.h>
#include <driver/ledc.h>
#include <hardware/Debug.h>

class PWM
{
  static constexpr uint32_t FREQ_HZ = 20000;
  static constexpr ledc_timer_bit_t RESOLUTION = LEDC_TIMER_10_BIT;
  static constexpr uint32_t MAX_DUTY = (1 << RESOLUTION) - 1;

  uint8_t _pin;
  ledc_channel_t _channel;
  ledc_timer_t _timer;
  Debug &dbg;

  uint32_t _duty = MAX_DUTY;
  bool _on = false;

  void applyDuty(uint32_t duty)
  {
    ledc_set_duty(LEDC_LOW_SPEED_MODE, _channel, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, _channel);
  }

public:
  explicit PWM(uint8_t pin, Debug &dbg,
               ledc_channel_t channel = LEDC_CHANNEL_0,
               ledc_timer_t timer = LEDC_TIMER_0)
      : _pin(pin), _channel(channel), _timer(timer), dbg(dbg) {}

  void init()
  {
    ledc_timer_config_t timerCfg = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = RESOLUTION,
        .timer_num = _timer,
        .freq_hz = FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timerCfg);

    ledc_channel_config_t channelCfg = {
        .gpio_num = _pin,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = _channel,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = _timer,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&channelCfg);
  }

  void on()
  {
    _on = true;
    applyDuty(_duty);
  }

  void off()
  {
    _on = false;
    applyDuty(0);
  }

  void max() { power(100); }

  void power(uint8_t percents)
  {
    if (percents > 100) percents = 100;
    uint32_t newDuty = map(percents, 0, 100, 0, MAX_DUTY);
    if (newDuty == _duty && _on)
      return;
    _duty = newDuty;
    _on = percents > 0;
    applyDuty(_duty);
    dbg.print(String("PWM:") + percents + "% duty=" + _duty);
  }
};
