#pragma once
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "StateMachine.hpp"

// Shared handles and cross-task state, created once in app_main and passed to
// every task by pointer. The table of what crosses a task boundary and what
// protects it is in docs/architecture.md §2.

class ITransport;
class ConfigStore;

enum class EstopSource : uint8_t { None, Button, Uart };

struct Ipc
{
    // --- FreeRTOS objects (static storage lives in main.cpp) -----------------
    QueueHandle_t     cmdQ       = nullptr; // link_uart, ui -> ctrl
    QueueHandle_t     logQ       = nullptr; // ctrl, link_uart -> logger (drop-oldest)
    SemaphoreHandle_t cfgMutex   = nullptr; // guards ConfigStore's blob
    TaskHandle_t      safetyTask = nullptr; // target of the E-stop notification

    // --- shared services ---------------------------------------------------
    ITransport  *link   = nullptr; // writeLine() is TX-mutex guarded
    ConfigStore *config = nullptr;

    // --- published state (single writer each) -------------------------------
    std::atomic<State>       state{State::Boot};          // writer: ctrl
    std::atomic<bool>        estopLatched{false};         // writer: safety
    std::atomic<EstopSource> estopSource{EstopSource::None};
    std::atomic<bool>        linkFresh{false};            // writer: ctrl

    // --- receiver / drop counters (docs/protocol.md §5) --------------------
    // Monotonic since boot, never fatal. Exposed in tlm.sys.link and on the OLED.
    std::atomic<uint32_t> badCrc{0};       // NDJSON line with a wrong/missing *XX
    std::atomic<uint32_t> overlong{0};     // line exceeded 256 B, discarded to newline
    std::atomic<uint32_t> unparsed{0};     // unknown tag/type, malformed, wrong field count
    std::atomic<uint32_t> outOfRange{0};   // a field failed the §2.3 / §3.3 range check
    std::atomic<uint32_t> dropInactive{0}; // a valid frame on a non-selected channel
    std::atomic<uint32_t> uartErr{0};      // driver framing error / overrun / break
    std::atomic<uint32_t> logDropped{0};   // log_q drop-oldest

    // Latest control sample for the tlm line. Single writer (ctrl), lossy reader
    // (link_uart) - telemetry tolerates a torn float, so no lock. The full
    // tlm.sd / tlm.sys schema is tasks #4 / #5.
    struct TelemSample
    {
        float ex, ey, vpan, vtilt, pan, tilt;
    };
    TelemSample telem{};
};
