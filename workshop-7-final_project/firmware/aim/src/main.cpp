#include <cstdio>
#include <cstring>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <nvs_flash.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>

#include "Config.hpp"
#include "Pinout.hpp"
#include "ipc/Ipc.hpp"
#include "ipc/CmdQueue.hpp"
#include "ipc/LogQueue.hpp"
#include "ConfigStore.hpp"
#include "drivers/Sdcard.hpp"
#include "transport/UartTransport.hpp"
#include "transport/Ndjson.hpp"
#include "tasks/Safety.hpp"
#include "tasks/Ctrl.hpp"
#include "tasks/LinkUart.hpp"
#include "tasks/Logger.hpp"
#include "tasks/Ui.hpp"

// AIM entry point. Everything is statically allocated:
// no heap request happens after this function returns, and a failed one before
// then is caught by the hooks below.

static const char *TAG = "AIM";

// --- shared handles + services (ipc first: everything else binds a reference) --
static Ipc          ipc;
static ConfigStore  configStore;
static UartTransport uart;
static Sdcard       sdCard; // logger owns mount/retry - see tasks/Logger.hpp

// --- task objects ---------------------------------------------------------
static SafetyTask   safetyTask{ipc};
static CtrlTask     ctrlTask{ipc};
static LinkUartTask linkUartTask{ipc};
static LoggerTask   loggerTask{ipc, sdCard};
static UiTask       uiTask{ipc};

// --- static task stacks + control blocks -------------------------------------
static StackType_t  s_safetyStack[config::STACK_SAFETY];
static StaticTask_t s_safetyTcb;
static StackType_t  s_ctrlStack[config::STACK_CTRL];
static StaticTask_t s_ctrlTcb;
static StackType_t  s_linkStack[config::STACK_LINK_UART];
static StaticTask_t s_linkTcb;
static StackType_t  s_uiStack[config::STACK_UI];
static StaticTask_t s_uiTcb;
static StackType_t  s_loggerStack[config::STACK_LOGGER];
static StaticTask_t s_loggerTcb;

static TaskHandle_t s_hCtrl, s_hLink, s_hUi, s_hLogger;

// --- static queue + mutex storage ----------------------------------------
static uint8_t       s_cmdQStore[config::CMD_Q_LEN * sizeof(CmdItem)];
static StaticQueue_t s_cmdQBuf;
static uint8_t       s_logQStore[config::LOG_Q_LEN * sizeof(LogRecord)];
static StaticQueue_t s_logQBuf;
static StaticSemaphore_t s_cfgMutexBuf;

// --- fail-loud on a heap or stack failure: a silent one is worse than a reboot -
//
// ESP-IDF routes allocation failures through the heap component, not FreeRTOS's
// configUSE_MALLOC_FAILED_HOOK (which it hard-defines to 0 and does not expose in
// Kconfig), so the real hook here is heap_caps_register_failed_alloc_callback(),
// registered in app_main. vApplicationMallocFailedHook stays defined too - it
// costs nothing and documents the intent for a plain-FreeRTOS reader.
extern "C" void vApplicationMallocFailedHook(void)
{
    esp_rom_printf("HOOK: malloc failed after init - restarting\n");
    esp_restart();
}

extern "C" void vApplicationStackOverflowHook(TaskHandle_t task, char *name)
{
    (void)task;
    esp_rom_printf("HOOK: stack overflow in '%s' - restarting\n", name ? name : "?");
    esp_restart();
}

static void onHeapAllocFailed(size_t size, uint32_t caps, const char *fn)
{
    esp_rom_printf("HOOK: alloc of %u bytes (caps 0x%x) failed in %s - restarting\n",
                   (unsigned)size, (unsigned)caps, fn ? fn : "?");
    esp_restart();
}

static const char *resetReasonStr(esp_reset_reason_t r)
{
    switch (r)
    {
    case ESP_RST_POWERON:   return "POWERON";
    case ESP_RST_SW:        return "SW";
    case ESP_RST_PANIC:     return "PANIC";
    case ESP_RST_INT_WDT:   return "INT_WDT";
    case ESP_RST_TASK_WDT:  return "TASK_WDT";
    case ESP_RST_WDT:       return "WDT";
    case ESP_RST_BROWNOUT:  return "BROWNOUT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_USB:       return "USB";
    default:                return "OTHER";
    }
}

// A hardware/software watchdog reset, as opposed to a plain power-on or SW
// (esp_restart()) reset. ESP_RST_PANIC is deliberately excluded here -
// it also fires for asserts/aborts unrelated to a stalled task, so it is not
// a reliable "the watchdog got us" signal even though TWDT itself panics.
static bool isWatchdogReset(esp_reset_reason_t r)
{
    return r == ESP_RST_TASK_WDT || r == ESP_RST_INT_WDT || r == ESP_RST_WDT;
}

static void emitBootEvent(bool usedDefaults, uint32_t wdtResets)
{
    const char *reason = resetReasonStr(esp_reset_reason());

    char line[160];
    std::snprintf(line, sizeof(line),
                  "{\"t\":\"evt\",\"e\":\"boot\",\"up\":0,\"reason\":\"%s\",\"ver\":%u,\"wdt_resets\":%lu}",
                  reason, (unsigned)config::SCHEMA_VERSION, (unsigned long)wdtResets);
    ndjson::seal(line, sizeof(line));
    uart.writeLine(line);

    LogRecord r{};
    r.kind      = LogRecord::Kind::Boot;
    r.t_mono_us = 0;
    std::strncpy(r.note, reason, sizeof(r.note) - 1);
    logSend(ipc.logQ, &r, &ipc.logDropped);

    ESP_LOGI(TAG, "boot: reason=%s schema=v%u config=%s", reason,
             (unsigned)config::SCHEMA_VERSION, usedDefaults ? "defaults" : "nvs");
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "AIM starting");

    ESP_ERROR_CHECK(heap_caps_register_failed_alloc_callback(onHeapAllocFailed));

    // ~1 s TWDT. ctrl and safety subscribe themselves at the top of their own
    // run() (tasks/Ctrl.hpp, tasks/Safety.hpp); this just sets the timeout and
    // turns a timeout into a panic-reset instead of a warning - a stalled
    // task must reset the board, not leave it wedged with the laser possibly
    // still lit.
    const esp_task_wdt_config_t twdtConfig = {
        .timeout_ms     = config::WDT_TIMEOUT_MS,
        .idle_core_mask = (1u << 0) | (1u << 1), // both cores' idle tasks
        .trigger_panic  = true,
    };
    ESP_ERROR_CHECK(esp_task_wdt_reconfigure(&twdtConfig));

    esp_err_t nvs = nvs_flash_init();
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    else
    {
        ESP_ERROR_CHECK(nvs);
    }

    ipc.cfgMutex = xSemaphoreCreateMutexStatic(&s_cfgMutexBuf);
    const bool usedDefaults = configStore.load(ipc.cfgMutex);
    ipc.config = &configStore;

    // A watchdog reset means ctrl or safety hung mid-loop, possibly while
    // armed. Never resume tracking silently - force the channel back to
    // NONE (persisted, same path as an NDJSON cfg.set) so an operator has to
    // consciously re-arm. The FSM itself always boots into SELFTEST then
    // DISARMED/ZONE_TOUR regardless (see StateMachine.hpp / Ctrl::run), so
    // this only has to answer for the *channel*, not the state.
    const esp_reset_reason_t resetReason = esp_reset_reason();
    uint32_t                 wdtResets   = configStore.wdtResetCount();
    if (isWatchdogReset(resetReason))
    {
        wdtResets = configStore.bumpWdtResetCount();
        const ConfigStore::Result r =
            configStore.set("input.channel", ConfigStore::Value::string("NONE"));
        ESP_LOGW(TAG, "watchdog reset (reason=%s) - channel forced to NONE (%s), wdt_resets=%lu",
                 resetReasonStr(resetReason), r.ok ? "ok" : (r.err ? r.err : "?"),
                 (unsigned long)wdtResets);
    }

    uart.setCounters(&ipc.overlong, &ipc.uartErr);
    uart.init();
    ipc.link = &uart;

    ipc.cmdQ = xQueueCreateStatic(config::CMD_Q_LEN, sizeof(CmdItem), s_cmdQStore, &s_cmdQBuf);
    ipc.logQ = xQueueCreateStatic(config::LOG_Q_LEN, sizeof(LogRecord), s_logQStore, &s_logQBuf);

    // Hardware + config-derived setup, while still single-threaded.
    ctrlTask.init();
    uiTask.init();

    // safety first, so the E-stop ISR is armed before ctrl starts driving.
    ipc.safetyTask = xTaskCreateStaticPinnedToCore(
        &SafetyTask::entry, "safety", config::STACK_SAFETY, &safetyTask,
        config::PRIO_SAFETY, s_safetyStack, &s_safetyTcb, config::CORE_SAFETY);
    installEstopIsr(ipc);
    emitBootEvent(usedDefaults, wdtResets);

    s_hLogger = xTaskCreateStaticPinnedToCore(
        &LoggerTask::entry, "logger", config::STACK_LOGGER, &loggerTask,
        config::PRIO_LOGGER, s_loggerStack, &s_loggerTcb, config::CORE_IO);
    s_hLink = xTaskCreateStaticPinnedToCore(
        &LinkUartTask::entry, "link_uart", config::STACK_LINK_UART, &linkUartTask,
        config::PRIO_LINK_UART, s_linkStack, &s_linkTcb, config::CORE_IO);
    s_hUi = xTaskCreateStaticPinnedToCore(
        &UiTask::entry, "ui", config::STACK_UI, &uiTask,
        config::PRIO_UI, s_uiStack, &s_uiTcb, config::CORE_IO);
    s_hCtrl = xTaskCreateStaticPinnedToCore(
        &CtrlTask::entry, "ctrl", config::STACK_CTRL, &ctrlTask,
        config::PRIO_CTRL, s_ctrlStack, &s_ctrlTcb, config::CORE_CTRL);

    // Published for ui's periodic (config::SYS_STATS_PERIOD_MS) CPU% / stack
    // high-water report. Set once, after every task exists - ui's report
    // guards against a null handle so it can never race this assignment.
    ipc.ctrlTask   = s_hCtrl;
    ipc.linkTask   = s_hLink;
    ipc.loggerTask = s_hLogger;
    ipc.uiTask     = s_hUi;

    ESP_LOGI(TAG, "tasks up - ctrl@c%d/p%d, safety@c%d/p%d, io tasks@c%d",
             config::CORE_CTRL, config::PRIO_CTRL, config::CORE_SAFETY, config::PRIO_SAFETY,
             config::CORE_IO);
}
