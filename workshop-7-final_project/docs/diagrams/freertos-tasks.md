# FreeRTOS tasks — AIM

The `AIM` (ESP32-S3) task set: which core each task runs on, and what
crosses between them. The task table (priority, period, what each task
owns) and the full "what crosses a boundary, and what protects it"
reference are in
[`architecture.md` §2](../architecture.md#2-task-architecture--aim) — this
page is the diagram only.

```mermaid
flowchart LR
    estopISR(("E-stop ISR"))

    subgraph Core0["Core 0"]
        link_uart[["link_uart<br/>normal · event-driven"]]
        logger[["logger<br/>low · drains log_q"]]
        ui[["ui<br/>low · 100 ms"]]
    end

    subgraph Core1["Core 1"]
        ctrl[["ctrl<br/>high · 20 ms"]]
        safety[["safety<br/>realtime · 250 ms wait"]]
    end

    link_uart -->|cmd_q| ctrl
    ui -->|cmd_q| ctrl
    ctrl -->|log_q| logger
    link_uart -->|log_q| logger
    link_uart -->|ConfigStore mutex| ctrl
    ui -->|ConfigStore mutex| ctrl
    ctrl -->|Fsm::state atomic| ui
    ctrl -->|Fsm::state atomic| link_uart
    ctrl -->|Fsm::state atomic| safety
    safety -->|estopLatched atomic| ctrl
    ctrl -->|linkFresh atomic| safety
    ctrl -->|counters atomic| ui
    ctrl -->|TelemSample| link_uart
    logger -->|Sd health| link_uart
    logger -->|Sd health| ui
    estopISR -->|task notify| safety
    link_uart -->|estop notify| safety
```

Reading the diagram: boxes are tasks, grouped by the core they are pinned
to; each arrow is one item from the "what crosses a boundary" table in
`architecture.md` §2, labelled with the queue, mutex or atomic that carries
it.
