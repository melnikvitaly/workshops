#pragma once
#include <Arduino.h>
#include "driver/ledc.h"
#include "Debug.h"

class L298NMotor
{
public:
    enum class DIRECTION
    {
        STOP = 0,
        FORWARD = 1,
        BACKWARD = -1,
    };
    constexpr static uint8_t MAX_SPEED = 100;

private:
    // Shared timer config: 20 kHz, 8-bit resolution on timer 0
    static constexpr ledc_mode_t    LEDC_MODE       = LEDC_LOW_SPEED_MODE;
    static constexpr ledc_timer_t   LEDC_TIMER_IDX  = LEDC_TIMER_0;
    static constexpr uint32_t       LEDC_FREQ_HZ    = 20000;
    static constexpr ledc_timer_bit_t LEDC_RES      = LEDC_TIMER_8_BIT;
    static constexpr uint32_t       LEDC_MAX_DUTY   = (1 << 8) - 1; // 255

    std::array<uint8_t, 3> _pins; // EN(PWM), IN1, IN2
    ledc_channel_t _channel;
    uint32_t _duty = LEDC_MAX_DUTY;
    DIRECTION _direction = DIRECTION::STOP;
    Debug &_dbg;

    static ledc_channel_t allocChannel()
    {
        static int next = 0;
        return static_cast<ledc_channel_t>(next++);
    }

    void applyDuty()
    {
        ledc_set_duty(LEDC_MODE, _channel, _duty);
        ledc_update_duty(LEDC_MODE, _channel);
    }

public:
    L298NMotor(std::array<uint8_t, 3> pins, Debug &dbg)
        : _pins(pins), _channel(allocChannel()), _dbg(dbg) {}

    void init()
    {
        static bool timerReady = false;
        if (!timerReady)
        {
            ledc_timer_config_t timer = {};
            timer.speed_mode      = LEDC_MODE;
            timer.duty_resolution = LEDC_RES;
            timer.timer_num       = LEDC_TIMER_IDX;
            timer.freq_hz         = LEDC_FREQ_HZ;
            timer.clk_cfg         = LEDC_AUTO_CLK;
            ESP_ERROR_CHECK(ledc_timer_config(&timer));
            timerReady = true;
        }

        ledc_channel_config_t ch = {};
        ch.gpio_num   = _pins[0];
        ch.speed_mode = LEDC_MODE;
        ch.channel    = _channel;
        ch.intr_type  = LEDC_INTR_DISABLE;
        ch.timer_sel  = LEDC_TIMER_IDX;
        ch.duty       = 0;
        ch.hpoint     = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&ch));

        pinMode(_pins[1], OUTPUT);
        pinMode(_pins[2], OUTPUT);
    }

    void speed(uint8_t percent)
    {
        uint32_t newDuty = map(percent, 0, MAX_SPEED, 0, LEDC_MAX_DUTY);
        if (newDuty == _duty)
            return;
        _duty = newDuty;
        _dbg.print(String("speed(") + percent + "%," + _duty + ")");
        writeToPins();
    }

    void move(DIRECTION newDirection, uint8_t newSpeed = -1)
    {
        if (newSpeed != (uint8_t)-1)
            speed(newSpeed);
        _direction = newDirection;
        writeToPins();
    }

    void forward(uint8_t newSpeed = -1)
    {
        if (newSpeed != (uint8_t)-1)
            speed(newSpeed);
        _direction = DIRECTION::FORWARD;
        writeToPins();
    }

    void backward(uint8_t newSpeed = -1)
    {
        if (newSpeed != (uint8_t)-1)
            speed(newSpeed);
        _direction = DIRECTION::BACKWARD;
        writeToPins();
    }

    void stop()
    {
        _direction = DIRECTION::STOP;
        writeToPins();
    }

    void maxSpeed() { speed(MAX_SPEED); }

    void writeToPins()
    {
        if (_direction == DIRECTION::FORWARD)
        {
            _dbg.print(String("fwd(") + _duty + ")");
            applyDuty();
            digitalWrite(_pins[1], HIGH);
            digitalWrite(_pins[2], LOW);
        }
        else if (_direction == DIRECTION::BACKWARD)
        {
            _dbg.print(String("bwd(") + _duty + ")");
            applyDuty();
            digitalWrite(_pins[1], LOW);
            digitalWrite(_pins[2], HIGH);
        }
        else
        {
            _dbg.print(String("stop"));
            _duty = 0;
            applyDuty();
            digitalWrite(_pins[1], LOW);
            digitalWrite(_pins[2], LOW);
        }
    }
};
