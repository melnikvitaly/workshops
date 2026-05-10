#pragma once
#include <hardware/Debug.h>
#include <driver/gpio.h>
#include <esp_timer.h>
#include <esp_rom_sys.h>
#include <Config.h>
#include <string>

class Task
{
    static constexpr gpio_num_t BOUNCE_OUT_PIN  = GPIO_NUM_2;
    static constexpr gpio_num_t FILTERED_IN_PIN = GPIO_NUM_5;

    static constexpr int     BOUNCE_COUNT  = 10;
    static constexpr int64_t CONTACT_US    = 2;             // pin LOW (contact made)
    static constexpr int64_t RETURN_US     = 5;             // pin HIGH (contact lifted)
    static constexpr int64_t SETTLE_US     = 50;            // ~5τ for RC to settle t= 100R * 100nF = 10uS, my button on release generate peak t=600uS-1400uS and also peaks 310ns
    static constexpr int64_t PRESS_HOLD_US = 1000LL * 1000; // 1 s
    static constexpr int64_t IDLE_US       = 2000LL * 1000; // 2 s between cycles
    static constexpr int     MAX_EDGES     = 64;

    enum class SimState { IDLE, PRESSED };

public:
    Debug dbg{"task5"};

    void setup()
    {
        gpio_config_t out_conf = {
            .pin_bit_mask = (1ULL << BOUNCE_OUT_PIN),
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
        };
        gpio_config(&out_conf);
        gpio_set_level(BOUNCE_OUT_PIN, 1); // idle = HIGH (active-low)

        // ANYEDGE ISR — fires even while CPU is busy in generateBounce()
        gpio_config_t in_conf = {
            .pin_bit_mask = (1ULL << FILTERED_IN_PIN),
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_ANYEDGE,
        };
        gpio_config(&in_conf);
        gpio_install_isr_service(0);
        gpio_isr_handler_add(FILTERED_IN_PIN, edgeIsr, this);

        dbg.print("Started. Wire: GPIO2 --[100R]--> node --[100n]--> GND, node --> GPIO5");
    }

    void loop()
    {
        int64_t now = esp_timer_get_time();

        switch (_simState)
        {
        case SimState::IDLE:
            if ((now - _simEnteredUs) >= IDLE_US)
            {
                _edgeCount = 0;

                dbg.print("--- press bounce start ---");
                _bounceRefUs = esp_timer_get_time(); // set AFTER print so offset is accurate
                generateBounce(0);                   // contact level = LOW
                uint32_t pressBounceEdges = _edgeCount;

                gpio_set_level(BOUNCE_OUT_PIN, 0);   // settle LOW = pressed
                esp_rom_delay_us(SETTLE_US);
                uint32_t pressSettleEdges = _edgeCount - pressBounceEdges;

                dbg.print("press bounce: " + edgeReport(0, pressBounceEdges));
                dbg.print("press settle: " + std::to_string(pressSettleEdges) + " (ideal=1)");

                _simState     = SimState::PRESSED;
                _simEnteredUs = now;
            }
            break;

        case SimState::PRESSED:
            if ((now - _simEnteredUs) >= PRESS_HOLD_US)
            {
                uint32_t priorEdges = _edgeCount;

                dbg.print("--- release bounce start ---");
                _bounceRefUs = esp_timer_get_time(); // set AFTER print
                generateBounce(1);                   // contact level = HIGH
                uint32_t relBounceEdges = _edgeCount - priorEdges;

                gpio_set_level(BOUNCE_OUT_PIN, 1);   // settle HIGH = released
                esp_rom_delay_us(SETTLE_US);
                uint32_t relSettleEdges = _edgeCount - priorEdges - relBounceEdges;

                dbg.print("release bounce: " + edgeReport(priorEdges, priorEdges + relBounceEdges));
                dbg.print("release settle: " + std::to_string(relSettleEdges) + " (ideal=1)");
                dbg.print("total: " + std::to_string(_edgeCount) + " (ideal=2)");
                dbg.print("--- released ---");

                _simState     = SimState::IDLE;
                _simEnteredUs = now;
            }
            break;
        }
    }

private:
    SimState          _simState     = SimState::IDLE;
    int64_t           _simEnteredUs = 0;
    int64_t           _bounceRefUs  = 0;
    volatile uint32_t _edgeCount    = 0;
    volatile int64_t  _edgeTimes[MAX_EDGES];

    // Formats edges [startIdx, endIdx) with timings relative to _bounceRefUs
    std::string edgeReport(uint32_t startIdx, uint32_t endIdx)
    {
        uint32_t count = endIdx - startIdx;
        std::string s  = std::to_string(count) + " edges";
        for (uint32_t i = startIdx; i < endIdx && i < (uint32_t)MAX_EDGES; i++)
        {
            s += (i == startIdx) ? " [+" : ", +";
            s += std::to_string(_edgeTimes[i] - _bounceRefUs);
            s += "us";
        }
        if (count > 0) s += "]";
        return s;
    }

    static void IRAM_ATTR edgeIsr(void *arg)
    {
        Task *self = static_cast<Task *>(arg);
        uint32_t idx = self->_edgeCount;
        if (idx < MAX_EDGES)
            self->_edgeTimes[idx] = esp_timer_get_time();
        self->_edgeCount = idx + 1;
    }

    void generateBounce(int contactLevel)
    {
        int returnLevel = 1 - contactLevel;
        for (int i = 0; i < BOUNCE_COUNT; i++)
        {
            gpio_set_level(BOUNCE_OUT_PIN, contactLevel);
            esp_rom_delay_us(CONTACT_US);
            gpio_set_level(BOUNCE_OUT_PIN, returnLevel);
            esp_rom_delay_us(RETURN_US);
        }
    }
};
