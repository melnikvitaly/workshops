# Feedback

- trigonometry is not taken into account (Seems already mention in SLIDE)
  - Resolved: confirmed not implemented anywhere in code (firmware or
    `eye/camera`); already documented as an open issue on presentation
    slide 10. No action needed.
- clamping of integral part of PID and max min angles (find best place to add to presentation hat limits and clamping are present in the system)
  - Resolved: both exist (`PositionalPid.hpp` anti-windup, `Gimbal.hpp` /
    `Config.hpp` angle limits) and are documented in `docs/architecture.md`
    §6 "Bounds and saturation". Added a row to presentation slide 7's
    decisions table for the PID anti-windup, next to the existing
    angle-limit row.
- dumpStackHighWater() is called only once in main?
  - Resolved: removed the one-shot call/function from `main.cpp`; periodic
    reporting already existed in `UiTask::reportTaskLoad()` (`Ui.hpp`), so
    nothing is lost. Also fixed docs (`TASKS.md`, `docs/coding.md`,
    `docs/architecture.md`, `docs/assestment/verification/Requirements.html`)
    that mis-stated the report cadence as "1 Hz" — the actual period is
    `SYS_STATS_PERIOD_MS` (5 s).
- (Presentation) Make large fonts more firendly for demonstration slides
