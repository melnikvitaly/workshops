#include "RegistersBasedLed.hpp"
#include <esp_check.h>
#include <esp_log.h>
#include <soc/timer_group_struct.h>
#include <soc/system_struct.h>
#include <soc/gpio_struct.h>
#include <soc/interrupts.h>
#include <soc/soc.h>           // DR_REG_INTERRUPT_CORE0_BASE, REG_WRITE
#include <Config.hpp>
// #define USE_REGISTER_INTR Interesting, but does not work
#ifdef USE_REGISTER_INTR
// CPU interrupt channel arbitrarily chosen — no conflict detection, unlike esp_intr_alloc().
static constexpr int CPU_INTR_CH = 16;

#  ifdef CONFIG_IDF_TARGET_ESP32C3
#    include <soc/interrupt_core0_reg.h>  // DR_REG_INTERRUPT_CORE0_BASE
// CLIC memory-mapped registers (ESP32-C3 RISC-V Core Local Interrupt Controller).
// Each source N occupies 4 consecutive bytes starting at CLIC_BASE + 0x1000 + 4*N:
//   [+0] clicintip  – pending flag (read-only for level sources)
//   [+1] clicintie  – enable
//   [+2] clicintattr – bit1: 0 = level-triggered, 1 = edge-triggered
//   [+3] clicintctl – upper nibble = level, lower nibble = priority
#    define CLIC_BASE        0x20001000UL
#    define CLIC_INT_IE(n)   (*(volatile uint8_t *)(CLIC_BASE + 0x1001 + 4*(n)))
#    define CLIC_INT_ATTR(n) (*(volatile uint8_t *)(CLIC_BASE + 0x1002 + 4*(n)))
#    define CLIC_INT_CTL(n)  (*(volatile uint8_t *)(CLIC_BASE + 0x1003 + 4*(n)))

// CLIC vector table slots are bare function pointers — no argument channel.
// A static shim holds `this` so the handler can recover the instance.
// Consequence: only one RegistersBasedLed can run at a time.
static RegistersBasedLed * s_instance;
void IRAM_ATTR timerIsrShim_() { RegistersBasedLed::timerIsr_(s_instance); }

#  else // ESP32-S3 — Xtensa
#    include <xtensa/xtensa_api.h>  // xt_set_interrupt_handler, xt_ints_on
#  endif
#endif // USE_REGISTER_INTR

#define LOG(fmt, ...) ESP_LOGI("RegistersBasedLed", fmt, ##__VA_ARGS__)

// ── Timer Group 1, Timer 0 ──────────────────────────────────────────────────
// TIMERG0 T0 is intentionally left free for LEDController::autoTimer_ (gptimer API).
// The gptimer driver allocates timers starting from TIMERG0, so claiming TIMERG1 T0
// here via direct register access avoids any driver/hardware conflict.
//
// Config register bit layout — identical on ESP32-C3 (tx_* fields) and ESP32-S3 (tn_* fields);
// only the C-struct field names differ, not the hardware bit positions.
//   [9]     use_xtal    – 0 = APB clock source, 1 = XTAL
//   [10]    alarm_en    – enables alarm; hardware self-clears this bit when alarm fires
//   [12]    divcnt_rst  – write 1 to reset the prescaler counter
//   [28:13] divider     – clock prescaler: APB_HZ / TIMER_RESOLUTION_HZ = 80
//   [29]    autoreload  – counter resets to load_lo/hi on every alarm
//   [30]    increase    – 1 = count up, 0 = count down
//   [31]    en          – counter enable

static constexpr int TIDX = 0; // timer index within the group

// APB clock is 80 MHz on both ESP32-C3 and ESP32-S3 at default CPU frequencies.
// Prescaler: 80 MHz / 1 MHz = 80 ticks → 1 µs per tick.
static constexpr uint32_t APB_HZ  = 80'000'000u;
static constexpr uint32_t DIVIDER = APB_HZ / Config::TIMER_RESOLUTION_HZ; // = 80

// GPIO write-1-to-set / write-1-to-clear helpers.
// C3: out_w1ts/out_w1tc are unions (access via .val).
// S3: out_w1ts/out_w1tc are plain uint32_t.
// Both boards use LED pins in the GPIO 0-31 range (C3: GPIO3, S3: GPIO10).
#ifdef CONFIG_IDF_TARGET_ESP32C3
#  define GPIO_HIGH(pin) do { GPIO.out_w1ts.val = (1u << (pin)); } while (0)
#  define GPIO_LOW(pin)  do { GPIO.out_w1tc.val = (1u << (pin)); } while (0)
#else  // ESP32-S3
#  define GPIO_HIGH(pin) do { GPIO.out_w1ts = (1u << (pin)); } while (0)
#  define GPIO_LOW(pin)  do { GPIO.out_w1tc = (1u << (pin)); } while (0)
#endif

// active-LOW wiring: driving the pin LOW turns the LED on
#define LED_ON(pin)  GPIO_LOW(pin)
#define LED_OFF(pin) GPIO_HIGH(pin)

RegistersBasedLed::RegistersBasedLed(gpio_num_t pin) : pin_(pin) {}

// ── ISR ──────────────────────────────────────────────────────────────────────

void IRAM_ATTR RegistersBasedLed::timerIsr_(void *ctx)
{
    auto *self = static_cast<RegistersBasedLed *>(ctx);

    // Clear the T0 alarm interrupt flag. This must happen before re-arming the
    // alarm so that a spurious re-entry is avoided if the handler takes longer
    // than one period (which should never happen at 10 kHz / 100 µs period).
    TIMERG1.int_clr_timers.t0_int_clr = 1;

    PwmState &s = self->pwm_;
    uint64_t nextAlarm;

    if (s.ledOn) {
        LED_OFF(self->pin_);
        nextAlarm   = s.offTicks;
        s.ledOn = false;
    } else {
        LED_ON(self->pin_);
        nextAlarm   = s.onTicks;
        s.ledOn = true;
    }

    // Update alarm for the next phase. auto_reload_on_alarm already reset the
    // counter to 0, so nextAlarm is the number of ticks until the next transition.
    TIMERG1.hw_timer[TIDX].alarmlo.val = (uint32_t)(nextAlarm & 0xFFFFFFFFu);
    TIMERG1.hw_timer[TIDX].alarmhi.val = (uint32_t)(nextAlarm >> 32);

    // Re-arm alarm – hardware cleared bit 10 when the alarm fired.
    // Writing baseCfg_ | en | alarm_en restores all fields atomically without a
    // read-modify-write, avoiding any tearing race with setDuty() on dual-core S3.
    TIMERG1.hw_timer[TIDX].config.val = self->baseCfg_ | (1u << 31) | (1u << 10);
}

// ── Private init helpers ──────────────────────────────────────────────────────

void RegistersBasedLed::initGpio_()
{
    // gpio_config sets the IO_MUX function and pin direction. Replacing it with
    // direct IO_MUX register writes is board-specific and out of scope; the
    // hot-path GPIO toggles in the ISR and setDuty() use registers directly.
    gpio_config_t io{};
    io.pin_bit_mask = 1ULL << pin_;
    io.mode         = GPIO_MODE_OUTPUT;
    io.pull_up_en   = GPIO_PULLUP_DISABLE;
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io.intr_type    = GPIO_INTR_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io));
    LED_OFF(pin_); // LED off at startup
}

void RegistersBasedLed::initClock_()
{
    // Write directly to the system clock/reset registers instead of calling
    // periph_module_enable(). Field names differ between C3 and S3 because the
    // SYSTEM struct headers are SoC-specific.
#ifdef CONFIG_IDF_TARGET_ESP32C3
    SYSTEM.perip_clk_en0.reg_timergroup1_clk_en = 1;
    // Pulse the module reset to put all TIMERG1 registers in a known state.
    SYSTEM.perip_rst_en0.reg_timergroup1_rst = 1;
    SYSTEM.perip_rst_en0.reg_timergroup1_rst = 0;
    // After reset, WDT flash-boot protection is re-enabled by hardware. Clear it
    // so the watchdog does not reset the chip if it goes unfed.
    TIMERG1.wdtconfig0.wdt_flashboot_mod_en = 0;
    // C3 has an additional internal clock gate for the GP timer separate from the
    // bus clock. Defaults to 1 after reset; set explicitly for clarity.
    TIMERG1.regclk.timer_clk_is_active = 1;
#else  // ESP32-S3
    SYSTEM.perip_clk_en0.timergroup1_clk_en = 1;
    SYSTEM.perip_rst_en0.timergroup1_rst = 1;
    SYSTEM.perip_rst_en0.timergroup1_rst = 0;
    TIMERG1.wdtconfig0.wdt_flashboot_mod_en = 0;
    // S3 has no separate internal GP-timer clock gate.
#endif
}

void RegistersBasedLed::initTimer_()
{
    // Base config: APB clock source (use_xtal=0), prescaler → 1 µs/tick,
    // count-up, auto-reload. alarm_en (bit 10) and en (bit 31) are OR-ed in
    // only when the timer is actually running.
    baseCfg_ = (DIVIDER << 13) | (1u << 29) | (1u << 30);

    // Apply base config and pulse divcnt_rst (bit 12) to align the prescaler
    // counter to this new configuration.
    TIMERG1.hw_timer[TIDX].config.val = baseCfg_ | (1u << 12);
    TIMERG1.hw_timer[TIDX].config.val = baseCfg_; // clear divcnt_rst

    // Reload value = 0: counter always resets to zero on every alarm.
    TIMERG1.hw_timer[TIDX].loadlo.val = 0;
    TIMERG1.hw_timer[TIDX].loadhi.val = 0;

    // Enable the T0 alarm interrupt inside the timer group hardware.
    // Required in addition to the CPU-level interrupt allocated in begin().
    TIMERG1.int_ena_timers.t0_int_ena = 1;
}

// ── Public begin ─────────────────────────────────────────────────────────────

void RegistersBasedLed::begin()
{
    initGpio_();
    initClock_();
    initTimer_();

#ifdef USE_REGISTER_INTR
    // ── Register-based interrupt wiring ──────────────────────────────────────
    // Step 1 (identical on both targets): write to the interrupt matrix to route
    // the TIMERG1 T0 peripheral source to CPU interrupt channel CPU_INTR_CH.
    // The matrix register address = base + 4 * source_index.
    REG_WRITE(DR_REG_INTERRUPT_CORE0_BASE + 4 * ETS_TG1_T0_LEVEL_INTR_SOURCE,
              CPU_INTR_CH);

#  ifdef CONFIG_IDF_TARGET_ESP32C3
    // Step 2 (C3 / RISC-V CLIC): patch our slot in the CLIC vector table.
    // mtvt CSR (0x307) holds the table base in CLIC vectored mode; the IDF
    // fills this at startup. Each slot is a bare 32-bit function pointer —
    // no argument is passed, hence the static shim above.
    uint32_t mtvt;
    __asm__ volatile("csrr %0, 0x307" : "=r"(mtvt));
    reinterpret_cast<void (**)()>(mtvt)[CPU_INTR_CH] = timerIsrShim_;

    // Step 3 (C3): configure level-triggered, priority/level = 1, then enable.
    CLIC_INT_ATTR(CPU_INTR_CH) = 0;          // bit1 = 0 → level-triggered
    CLIC_INT_CTL(CPU_INTR_CH)  = (1u << 4);  // upper nibble = level 1
    s_instance = this;
    CLIC_INT_IE(CPU_INTR_CH)   = 1;

#  else // ESP32-S3 — Xtensa
    // Step 2 (S3 / Xtensa): register the handler + arg in the Xtensa interrupt
    // dispatch table (_xt_interrupt_table). The low-level _xt_interrupt_entry
    // thunk reads this table on every interrupt and forwards arg as the parameter.
    xt_set_interrupt_handler(CPU_INTR_CH,
                             [](void *arg) { timerIsr_(arg); },
                             this);

    // Step 3 (S3): unmask CPU interrupt channel by setting its bit in INTENABLE.
    xt_ints_on(1u << CPU_INTR_CH);
#  endif

    isrHandle_ = nullptr; // no IDF handle allocated in register mode

#else // default: IDF allocator ─────────────────────────────────────────────
    // esp_intr_alloc handles interrupt matrix routing, channel conflict
    // detection, vector table registration, and IRAM placement in one call.
    ESP_ERROR_CHECK(esp_intr_alloc(ETS_TG1_T0_LEVEL_INTR_SOURCE,
                                   ESP_INTR_FLAG_IRAM,
                                   timerIsr_, this, &isrHandle_));
#endif // USE_REGISTER_INTR

    setDuty(0); // start with LED off; also ensures counter is stopped cleanly
    LOG("init ok pin=%d div=%" PRIu32, pin_, DIVIDER);
}

// ── Duty cycle control ────────────────────────────────────────────────────────

void RegistersBasedLed::setDuty(uint8_t duty)
{
    // Stop counter and disable alarm before touching shared state.
    // Clearing both en (bit 31) and alarm_en (bit 10) prevents a pending or
    // in-flight ISR from writing stale alarm values after we update pwm_.
    TIMERG1.hw_timer[TIDX].config.val = baseCfg_; // en=0, alarm_en=0

    duty_ = duty;

    if (duty == 0) {
        LED_OFF(pin_);
        return;
    }
    if (duty == 100) {
        LED_ON(pin_);
        return;
    }

    // periodTicks = 1 MHz / 10 kHz = 100 ticks per full period.
    constexpr uint64_t periodTicks =
        (uint64_t)Config::TIMER_RESOLUTION_HZ / Config::PWM_FREQ_HZ;

    pwm_.onTicks  = periodTicks * duty / 100;
    pwm_.offTicks = periodTicks - pwm_.onTicks;

    // Start in ON phase so brightness is visible immediately after a duty change.
    LED_ON(pin_);
    pwm_.ledOn = true;

    // Program the alarm for the ON-phase duration.
    TIMERG1.hw_timer[TIDX].alarmlo.val = (uint32_t)(pwm_.onTicks & 0xFFFFFFFFu);
    TIMERG1.hw_timer[TIDX].alarmhi.val = (uint32_t)(pwm_.onTicks >> 32);

    // Reset counter to 0 so the first alarm fires exactly onTicks from now.
    // Writing any value to the load register triggers an immediate counter load
    // from loadlo/loadhi (both set to 0 in begin()).
    TIMERG1.hw_timer[TIDX].load.val = 1;

    // Start counter and arm alarm in one write to avoid a window where en=1
    // but alarm_en=0 (counter would run but never generate alarms).
    TIMERG1.hw_timer[TIDX].config.val = baseCfg_ | (1u << 31) | (1u << 10);

    LOG("duty=%u%% on=%llu off=%llu ticks", duty,
        (unsigned long long)pwm_.onTicks, (unsigned long long)pwm_.offTicks);
}
