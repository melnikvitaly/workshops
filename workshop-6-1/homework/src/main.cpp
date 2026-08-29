#include <atomic>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "AlertController.hpp"
#include "Clock.hpp"
#include "Config.hpp"
#include "hardware/ADC.hpp"
#include "hardware/Button.hpp"
#include "hardware/Debug.hpp"
#include "hardware/Led.hpp"
#include "hardware/PWM.hpp"

#pragma region PINS
static constexpr gpio_num_t    LED1_PIN    = GPIO_NUM_4;
static constexpr gpio_num_t    LED2_PIN    = GPIO_NUM_3;
static constexpr gpio_num_t    LED3_PIN    = GPIO_NUM_2;
static constexpr gpio_num_t    BTN_PIN     = GPIO_NUM_5;
static constexpr adc_channel_t POT_CHANNEL = ADC_CHANNEL_0; // GPIO0 on the C3
#pragma endregion

static Debug dbg("main");

static PWM pwm1(LED1_PIN, dbg.Scoped("pwm1"), LEDC_CHANNEL_1, LEDC_TIMER_1);
static PWM pwm2(LED2_PIN, dbg.Scoped("pwm2"), LEDC_CHANNEL_2, LEDC_TIMER_1);
static PWM pwm3(LED3_PIN, dbg.Scoped("pwm3"), LEDC_CHANNEL_0, LEDC_TIMER_0);

static LED led1(pwm1, dbg.Scoped("led1"));
static LED led2(pwm2, dbg.Scoped("led2"));
static LED led3(pwm3, dbg.Scoped("led3"));

static Button          btn(BTN_PIN, 0 /* active LOW */, dbg.Scoped("btn"));
static ADC             pot(POT_CHANNEL, ADC_BITWIDTH_12, dbg.Scoped("pot"), 2);
static AlertController alertController(led1, led2, dbg.Scoped("alert"));

#pragma region TASK STATS
// Replaces LoopTracker from 2-3. There is no single loop rate to measure any
// more, so each task counts its own iterations and a low-priority task reports
// them together with the stack each task still has to spare.
enum TaskId : uint8_t
{
    TASK_BUTTON,
    TASK_ALERT,
    TASK_POT,
    TASK_COUNT
};

struct TaskStat
{
    const char           *name;
    TaskHandle_t          handle;
    std::atomic<uint32_t> iterations;
};

static TaskStat taskStats[TASK_COUNT] = {
    {"button", nullptr, 0},
    {"alert", nullptr, 0},
    {"pot", nullptr, 0},
};
#pragma endregion

static void buttonTask(void *)
{
    TickType_t lastWake = xTaskGetTickCount();
    for (;;)
    {
        btn.tick();
        taskStats[TASK_BUTTON].iterations.fetch_add(1, std::memory_order_relaxed);
        xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(Config::BUTTON_PERIOD_MS));
    }
}

static void alertTask(void *)
{
    TickType_t lastWake = xTaskGetTickCount();
    for (;;)
    {
        alertController.tick();
        led1.tick();
        led2.tick();
        taskStats[TASK_ALERT].iterations.fetch_add(1, std::memory_order_relaxed);
        xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(Config::ALERT_PERIOD_MS));
    }
}

static void potTask(void *)
{
    TickType_t lastWake = xTaskGetTickCount();
    for (;;)
    {
        led3.power(pot.percent());
        taskStats[TASK_POT].iterations.fetch_add(1, std::memory_order_relaxed);
        xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(Config::POT_PERIOD_MS));
    }
}

static void statsTask(void *)
{
    Debug      statsDbg  = dbg.Scoped("stats");
    TickType_t lastWake  = xTaskGetTickCount();
    uint32_t   lastCount[TASK_COUNT] = {0};
    uint32_t   lastAt    = nowMs();

    for (;;)
    {
        xTaskDelayUntil(&lastWake, pdMS_TO_TICKS(Config::STATS_PERIOD_MS));

        const uint32_t now     = nowMs();
        const uint32_t elapsed = now - lastAt;
        lastAt                 = now;

        for (uint8_t i = 0; i < TASK_COUNT; i++)
        {
            const uint32_t iterations = taskStats[i].iterations.load(std::memory_order_relaxed);
            const uint32_t done       = iterations - lastCount[i];
            lastCount[i]              = iterations;
            statsDbg.print("%s: %lu iters / %lums, stack free %u B",
                           taskStats[i].name,
                           static_cast<unsigned long>(done),
                           static_cast<unsigned long>(elapsed),
                           static_cast<unsigned>(uxTaskGetStackHighWaterMark(taskStats[i].handle)));
        }
    }
}

extern "C" void app_main()
{
    led1.init();
    led2.init();
    led3.init();
    pot.init();
    btn.init();

    btn.onPress([] { alertController.onButtonPress(); /* Will be called in the buttons task*/ });

    // Single-core C3: xTaskCreate is the right call — there is no second core to
    // pin to. On a dual-core part the same three lines become
    // xTaskCreatePinnedToCore(..., APP_CPU_NUM).
    xTaskCreate(buttonTask, "button", 3072, nullptr, 5, &taskStats[TASK_BUTTON].handle);
    xTaskCreate(alertTask, "alert", 3072, nullptr, 4, &taskStats[TASK_ALERT].handle);
    xTaskCreate(potTask, "pot", 3072, nullptr, 3, &taskStats[TASK_POT].handle);
    xTaskCreate(statsTask, "stats", 3072, nullptr, 1, nullptr);

    dbg.print("Started");
}
