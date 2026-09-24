#pragma once
#include <cstdint>
#include <driver/ledc.h>
#include <utils/ViewPort.hpp>
#include <utils/Gains.hpp>
#include <utils/Zone.hpp>

// Application-level tuning. Hardware wiring lives in Pinout.hpp; the reusable
// classes keep their own internal defaults, and this is the single place where
// all the "knobs" live.
namespace config
{
    // --- Servo / PWM ---------------------------------------------------------
    constexpr ledc_timer_bit_t SERVO_PWM_RES = LEDC_TIMER_14_BIT; // fine steps at 50 Hz
    constexpr uint32_t SERVO_FREQ_HZ = 50;                        // SG90 expects 50 Hz
    constexpr uint16_t SERVO_MIN_US = 500;                        // pulse at one end stop
    constexpr uint16_t SERVO_MAX_US = 2500;                       // pulse at other end stop
    constexpr float SERVO_MIN_ANGLE = 0.0f;
    constexpr float SERVO_MAX_ANGLE = 180.0f;

    // Hard ceiling on how fast each axis may ever rotate (deg/s), enforced by
    // the Gimbal on every velocity it is given, no matter who commanded it.
    //
    // This is protection, not tuning - keep it separate from the PID output
    // clamp below. An SG90 is spec'd around 0.1 s / 60 deg (~600 deg/s)
    // unloaded; derate hard for the arm's inertia, the laser's mass and supply
    // sag. Commanding faster than the servo can actually move means it silently
    // falls behind, the loop briefly runs open, and the current spike upsets
    // the shared supply.
    constexpr float SERVO_PAN_MAX_RATE = 240.0f;  // was 150: faster targeting
    constexpr float SERVO_TILT_MAX_RATE = 200.0f; // was 120; NOT yet checked on the
                                                  // rig - watch for the servo falling
                                                  // behind and for supply sag

    // LEDC resource assignment for the two servo channels.
    constexpr ledc_channel_t PAN_PWM_CHANNEL = LEDC_CHANNEL_0;
    constexpr ledc_timer_t PAN_PWM_TIMER = LEDC_TIMER_0;
    constexpr ledc_channel_t TILT_PWM_CHANNEL = LEDC_CHANNEL_1;
    constexpr ledc_timer_t TILT_PWM_TIMER = LEDC_TIMER_1;

    // The buzzer/Beeper from workshop-5 is dropped: this board has no pin
    // for it, and OLED + status LED are the feedback path.

    // --- Gimbal travel limits (degrees) --------------------------------------
    // Hard mechanical stops. The gimbal never commands outside these.
    constexpr float GIMBAL_PAN_MIN = 15.0f;
    constexpr float GIMBAL_PAN_MAX = 105.0f; // was 100: raised to let the working
                                             // zone reach 5 deg further LEFT.
                                             // NOT re-measured on the rig - confirm
                                             // the arm actually has travel here.
    constexpr float GIMBAL_TILT_MIN = 46.0f;
    constexpr float GIMBAL_TILT_MAX = 125.0f;

    // The hard mechanical stops above, bundled for passing to Gimbal.
    constexpr Zone GIMBAL_MECH_ZONE = {GIMBAL_PAN_MIN, GIMBAL_PAN_MAX,
                                        GIMBAL_TILT_MIN, GIMBAL_TILT_MAX};

    // Working zone: the sub-window inside the travel limits that the laser is
    // actually allowed to roam while tracking. Its centre is the park pose at
    // boot.
    //
    // Deliberately much smaller than the full travel. A closed loop pointing a
    // laser is exactly the situation where you want the reachable area bounded
    // by something other than the mechanics - a sign error, a bad gain or a
    // confused detector should run the dot into a soft edge inside the scene,
    // not sweep it across the whole room. Widen it once the loop is tuned and
    // the axis directions are confirmed.
    // Measured on this rig: INCREASING the tilt angle aims the laser LOWER.
    // Keep that in mind when moving the window - it is the opposite of what the
    // numbers suggest.
    constexpr float WORK_PAN_MIN = 45.0f;
    constexpr float WORK_PAN_MAX = 105.0f;  // 60 deg of pan, centred on 75
                                            // (was 95: extended 10 deg further LEFT,
                                            // since increasing pan aims left here)
    constexpr float WORK_TILT_MIN = 85.0f;
    constexpr float WORK_TILT_MAX = 115.0f; // 30 deg of tilt, centred on 100 (aims low)

    static_assert(WORK_PAN_MIN < WORK_PAN_MAX && WORK_TILT_MIN < WORK_TILT_MAX,
                  "working zone is empty or inverted");
    static_assert(WORK_PAN_MIN >= GIMBAL_PAN_MIN && WORK_PAN_MAX <= GIMBAL_PAN_MAX,
                  "working zone escapes the pan travel limits");
    static_assert(WORK_TILT_MIN >= GIMBAL_TILT_MIN && WORK_TILT_MAX <= GIMBAL_TILT_MAX,
                  "working zone escapes the tilt travel limits");

    constexpr static ViewPort InitialViewPort = ViewPort::fromBounds(
        WORK_PAN_MIN, WORK_PAN_MAX,
        WORK_TILT_MIN, WORK_TILT_MAX);

    // --- Axis geometry -------------------------------------------------------
    // Which way the dot physically moves when a servo angle INCREASES, as seen
    // by someone facing the scene. These are facts about how the horns are
    // mounted, not preferences - the zone tour uses them to walk the perimeter
    // clockwise, which is what makes a wrong one obvious.
    // Both VERIFIED on this rig: with these values the boot tour starts
    // top-left and runs clockwise, which it only does when both are correct.
    // (With PAN_ANGLE_AIMS_RIGHT true it started top-right and ran
    // counter-clockwise - the signature of a mirrored horizontal axis.)
    constexpr bool TILT_ANGLE_AIMS_DOWN = true;  // increasing tilt aims DOWN
    constexpr bool PAN_ANGLE_AIMS_RIGHT = false; // increasing pan aims LEFT

    // --- Zone tour -----------------------------------------------------------
    // The laser walks the perimeter of the working zone, so the operator can
    // see where it can reach before the loop takes over. It also shows which
    // way each axis moves, which is the thing you most want to know before
    // raising the gains. It runs after SELFTEST only if the `boot.tour` config
    // key is on (default off), or on demand via `control.zone_tour`.
    constexpr float ZONE_TOUR_RATE_DEG_S = 40.0f; // slow enough to follow by eye
    constexpr uint32_t ZONE_TOUR_DWELL_MS = 250;  // pause on each corner

    // --- PID (error vector -> servo velocity) --------------------------------
    // The vision system reports the error normalised to [-1, 1] across the
    // frame; each PID converts that into a servo rate in deg/s.
    //
    // Kp has units of (deg/s) per unit error. The stability ceiling is set by
    // the round-trip latency of the vision link, NOT by the mechanics:
    //
    //     Kp * k  <=  0.5 / T        (for ~60 degrees of phase margin)
    //
    // where T is the measured camera-to-ESP32 latency in seconds and k is the
    // plant gain in normalised-units per degree (measure it: command a known
    // 10 degree step, see how far the dot moves across the frame, divide).
    //
    // Example: k = 0.02, T = 0.15 s  ->  Kp <= 167. The defaults below sit well
    // under that so the loop is safe before you have measured your own numbers.
    // Tune P first, then I; leave D at zero unless the error stream is clean.
    // Ki is what closes the LAST bit of error. P alone shrinks its own output
    // as the error shrinks, so near the target it commands a rate too small to
    // overcome servo deadband and the dot simply stops short. The integral
    // keeps accumulating that small residual until the output is big enough to
    // break through - which is exactly the job it exists for.
    //
    // Sizing: the closed loop is s^2 + (k*Kp)s + k*Ki, so critical damping is
    // Ki = Kp^2 * k / 4. With Kp = 40 and k ~ 0.02 that is ~8. Start at half
    // and work up: raise until the residual closes in about a second, back off
    // if the dot starts hunting around the target.
    constexpr float PAN_KP = 40.0f;
    constexpr float PAN_KI = 4.0f;
    constexpr float PAN_KD = 0.0f;

    constexpr float TILT_KP = 35.0f; // tilt lifts the laser against gravity
    constexpr float TILT_KI = 4.0f;
    constexpr float TILT_KD = 0.0f;

    // Low-pass on the derivative term. Camera error is noisy and raw
    // differentiation amplifies that noise; 1.0 disables the filter.
    constexpr float PID_DERIV_ALPHA = 0.3f;

    // PID output clamp (deg/s): how fast the *loop* is allowed to drive each
    // axis. This is a tuning knob - lower it for gentler tracking - and it also
    // defines "saturated" for the anti-windup logic.
    constexpr float PAN_MAX_SLEW = 200.0f;  // was 120
    constexpr float TILT_MAX_SLEW = 160.0f; // was 90
    // Note: the error is at most 1.0, so the P term alone tops out at Kp deg/s.
    // These caps only bite when Kp (plus the integral) exceeds them.

    // --- PID (error vector -> servo angle delta, velocity-equation form) ----
    // AutoVelocityEquationChannel's alternative to the rate-output PID above -
    // see docs/servo-control-strategies.md and
    // docs/pid_controller_equations_positional_vs_velocity.md. Same normalised
    // [-1, 1] error input, but the PID output here IS a servo-angle delta in
    // degrees, added straight onto the current angle each frame - no rate,
    // no outer integration step.
    //
    // Because Kp/Ki/Kd map normalised error onto DEGREES here (not the unit
    // position fraction the old direct-position form used), they are scaled
    // by the working-zone half-extent (WORK_PAN_MAX-WORK_PAN_MIN)/2 = 30 deg
    // pan, (WORK_TILT_MAX-WORK_TILT_MIN)/2 = 15 deg tilt, from that form's
    // starting points.
    //
    // Kp -> half-extent tries to close all the error in a single sample - a
    // deadbeat response that only works with a truly instantaneous plant.
    // This loop's dead time (50-250 ms, docs/architecture.md SS6) spans
    // several frame periods, so a Kp anywhere near that overshoots on the
    // next sample instead of settling. Derate the same way as a
    // Ziegler-Nichols dead-time rule: Kp <= half-extent / (2N), N = dead time
    // in frame periods. Start well under that and raise; back off at the
    // first sign of hunting.
    //
    // Ki pulls in the last bit of steady-state offset (e.g. gravity droop on
    // tilt); keep it a fraction of Kp so it cannot itself force an overshoot
    // on a fast-moving target. Kd stays at zero - the noisy-error argument by
    // PID_DERIV_ALPHA above applies here too.
    //
    // NOT yet bench-tuned - starting points only.
    constexpr float PAN_VEQ_KP = 4.5f;
    constexpr float PAN_VEQ_KI = 1.5f;
    constexpr float PAN_VEQ_KD = 0.0f;

    constexpr float TILT_VEQ_KP = 1.8f;
    constexpr float TILT_VEQ_KI = 0.75f;
    constexpr float TILT_VEQ_KD = 0.0f;

    // VelocityEquationPid's running output tracks the actual servo angle and
    // is clamped to it - not a fixed pair of constants here, because the
    // working zone can change at runtime (Gimbal::setWorkingZone(), a
    // cfg.set from the UI). AutoVelocityEquationChannel reads the live zone
    // straight off the Gimbal each tick (see
    // AutoVelocityEquationChannel::syncOutputLimits()), so this clamp can
    // never fall out of sync with what Gimbal::moveTo() itself allows.

    // These must not exceed the Gimbal's hard rate ceiling. If they did, the
    // PID would believe it was still in range while the Gimbal was quietly
    // limiting the rate - so conditional integration would keep accumulating
    // against a limit it cannot see, which is exactly the windup the
    // anti-windup logic exists to prevent.
    static_assert(PAN_MAX_SLEW <= SERVO_PAN_MAX_RATE,
                  "PAN_MAX_SLEW exceeds SERVO_PAN_MAX_RATE: the PID would wind up "
                  "against a rate limit it cannot observe");
    static_assert(TILT_MAX_SLEW <= SERVO_TILT_MAX_RATE,
                  "TILT_MAX_SLEW exceeds SERVO_TILT_MAX_RATE: the PID would wind up "
                  "against a rate limit it cannot observe");

    // The tour steps the gimbal positionally, so it bypasses the velocity
    // clamp in setVelocity() and has to police its own rate.
    static_assert(ZONE_TOUR_RATE_DEG_S <= SERVO_PAN_MAX_RATE &&
                      ZONE_TOUR_RATE_DEG_S <= SERVO_TILT_MAX_RATE,
                  "ZONE_TOUR_RATE_DEG_S exceeds a servo rate ceiling");

    // Sign of each axis. A camera mounted upside down, mirrored, or with image
    // Y pointing down flips these; getting one wrong turns the loop into
    // positive feedback and the gimbal runs straight to its stop. Bring the
    // loop up at low Kp and check both axes move TOWARDS the target first.
    // True, and now corroborated twice over:
    //
    //   observed - with this false the dot ran AWAY from the target, the
    //   signature of positive feedback;
    //   derived  - PAN_ANGLE_AIMS_RIGHT is false, so increasing the pan angle
    //   moves the dot left. A positive dx (target right of the dot) therefore
    //   needs a NEGATIVE rate, which is what negating the error produces.
    //
    // Two independent observations agreeing is worth more than either alone.
    constexpr bool PAN_INVERT = true;

    // False, and the reasoning matters because getting it wrong is a runaway:
    //
    //   image Y grows DOWNWARDS (OpenCV), so dy > 0 means the target sits below
    //   the dot, and we need the dot to move down;
    //   on this rig INCREASING the tilt angle aims LOWER;
    //   a positive error gives a positive PID output, which increases the angle.
    //
    // So a positive dy already drives the dot the right way - no inversion. The
    // two "downwards" conventions cancel. Had they not, the loop would drive the
    // dot away from the target and straight into the travel limit.
    constexpr bool TILT_INVERT = false;

    // --- Tracking behaviour --------------------------------------------------
    // Below this error the axis is considered on-target: the controller freezes
    // and commands zero. Without it the loop hunts back and forth forever,
    // because an SG90 has roughly 1 degree of deadband and cannot actually land
    // on an arbitrary angle.
    //
    // This is the accuracy floor - the loop will never point better than this,
    // so it is the first thing to shrink if the dot stops short. It cannot go
    // below what the servo can physically resolve: ~1 degree of deadband is
    // ~0.02 in normalised units at k = 0.02, and asking for better than the
    // mechanism can deliver buys hunting, not precision.
    constexpr float TRACK_DEADZONE = 0.004f; // ~0.2% of the frame

    // No valid frame for this long -> stop moving and drop the PID state. The
    // laser is a 5 mW emitter on a powered gimbal; it must not keep coasting on
    // a stale error when the link dies.
    constexpr uint32_t TRACK_TIMEOUT_MS = 300;

    // --- Laser ---------------------------------------------------------------
    constexpr bool RELAY_ACTIVE_HIGH = false; // this module energizes on LOW
    // The camera can only measure the error while the dot is visible, so the
    // laser latches on at boot and stays on.
    constexpr bool LASER_ON_AT_BOOT = true;

    // Firing blanks the beam for this long and then restores it - the visible
    // event is the gap, because the laser is otherwise constant-on.
    //
    // Keep it SHORT. While the beam is dark the PC cannot see the dot, so it
    // reports valid=0 and the gimbal holds; long enough and the link timeout
    // fires and throws away the PID integral. The static_assert below keeps the
    // two in a sane relationship.
    constexpr uint32_t LASER_FIRE_BLANK_MS = 120;

    static_assert(LASER_FIRE_BLANK_MS < TRACK_TIMEOUT_MS,
                  "LASER_FIRE_BLANK_MS must stay below TRACK_TIMEOUT_MS: the dot is "
                  "invisible while blanked, so a longer gap trips the link failsafe "
                  "and discards the PID integral on every shot");

    // --- Control step timing -----------------------------------------------
    // The ctrl task runs at this period with vTaskDelayUntil; the servos only
    // accept a fresh pulse every 20 ms anyway.
    constexpr uint32_t UPDATE_PERIOD_MS = 20; // ~50 Hz
    constexpr float    UPDATE_PERIOD_S  = UPDATE_PERIOD_MS / 1000.0f;

    // --- Status LED colours --------------------------------------------------
    struct Rgb
    {
        uint8_t r, g, b;
    };
    constexpr Rgb LED_TRACKING = {0, 24, 0};  // green:  armed, target visible
    constexpr Rgb LED_LOST = {32, 12, 0};     // amber:  armed, no valid frame
    constexpr Rgb LED_DISARMED = {0, 0, 24};  // blue:   loop disarmed
    constexpr Rgb LED_TOUR = {20, 0, 24};     // violet: walking the zone at boot
    constexpr Rgb LED_FAULT = {40, 0, 0};     // red:    latched fault
    constexpr Rgb LED_PARKED = {2, 2, 2};     // dim:    idle

    // --- FSM timing --------------------------------------------------------
    // DISARMED + channel NONE, untouched for this long -> PARKED (servos idle,
    // laser off). Any button press leaves PARKED.
    constexpr uint32_t PARK_IDLE_MS = 30000;

    // --- ui buttons (MODE, CONTROL) - polled at 50 Hz by the ui task ---------
    constexpr uint32_t UI_DEBOUNCE_MS  = 30;
    constexpr uint32_t UI_LONGPRESS_MS = 1000; // MODE long press -> NONE

    // --- Task stack sizes (words) ----------------------------------------------
    // Static allocation: every task carries a StackType_t array this long.
    // Sized with headroom; uxTaskGetStackHighWaterMark is dumped at boot.
    constexpr uint32_t STACK_SAFETY     = 3072;
    constexpr uint32_t STACK_CTRL       = 4096;
    constexpr uint32_t STACK_LINK_UART  = 4096;
    constexpr uint32_t STACK_UI         = 4096;
    constexpr uint32_t STACK_LOGGER     = 6144; // FatFs + stdio buffers; batch buffer is in BSS

    // --- Task priorities / cores --------------------------------------------
    constexpr int PRIO_SAFETY    = 24; // realtime - just blocks on the notify
    constexpr int PRIO_CTRL      = 20; // high - hard 20 ms deadline
    constexpr int PRIO_LINK_UART = 10; // normal - event driven
    constexpr int PRIO_UI        = 4;  // low
    constexpr int PRIO_LOGGER    = 3;  // lowest - the only task allowed a long block
    constexpr int CORE_CTRL      = 1;
    constexpr int CORE_SAFETY    = 1;
    constexpr int CORE_IO        = 0;  // link_uart, ui, logger

    // --- Watchdog --------------------------------------------------------------
    // ctrl (hard 20 ms loop) and safety (blocks on the E-stop notify) are the
    // two tasks whose stall would leave the gimbal/laser in an unknown state,
    // so they are the ones subscribed to esp_task_wdt - see tasks/Ctrl.hpp and
    // tasks/Safety.hpp. Reconfigured at boot with trigger_panic=true: a stall
    // resets the board outright rather than leaving it wedged with the laser
    // possibly still lit.
    constexpr uint32_t WDT_TIMEOUT_MS = 1000;
    // How often safety re-arms the watchdog while idle-waiting on the E-stop
    // notification - well under WDT_TIMEOUT_MS so a scheduling hiccup alone
    // never trips it.
    constexpr uint32_t WDT_SAFETY_FEED_MS = 250;

    // --- Queue depths --------------------------------------------------------
    constexpr int CMD_Q_LEN = 24;
    constexpr int LOG_Q_LEN = 64; // deep, drop-oldest with a counter

    // --- SD logging ----------------------------------------------------------
    // One append-only LOG.CSV. logger batches whole blocks and f_syncs on a
    // timer - never per record. A card stall can block a single write 100-250 ms,
    // which is why logger is the lowest-priority task and the only long blocker.
    constexpr char     SD_MOUNT_POINT[]   = "/sdcard";
    constexpr char     SD_LOG_PATH[]      = "/sdcard/LOG.CSV";
    constexpr uint32_t SD_BATCH_BYTES     = 4096;  // flush at one FATFS_SECTOR_4096 block
    constexpr uint32_t SD_BATCH_MAX_BYTES = 8192;  // batch buffer ceiling (BSS)
    constexpr uint32_t SD_SYNC_SECONDS    = 5;     // f_sync cadence
    constexpr uint32_t SD_MOUNT_RETRY_MS  = 5000;  // retry a failed / absent mount
    constexpr uint32_t SD_LOOP_TICK_MS    = 200;   // log_q receive timeout - bounds the timers
    constexpr uint32_t SD_FREE_POLL_MS    = 2000;  // sd.free_bytes refresh

    // --- Diagnostics ---------------------------------------------------------
    // Per-task CPU% / stack high-water and free-heap are always collected -
    // ui's 1 Hz reportTaskLoad() and main.cpp's boot-time dump feed
    // ipc.taskLoad and tlm.sys either way, and that collection is cheap next
    // to the console traffic it produces. This flag gates ONLY the console
    // ESP_LOGI spam (the "cpu ctrl 1.7% stack free ..." lines and the boot
    // stack-high-water line) - off by default so a stock build's UART/console
    // isn't drowned in it. Flip on for bring-up / soak testing.
    constexpr bool ENABLE_SYS_STATS = false;

    // --- Config plane ------------------------------------------------------
    // Exactly one input channel is processed at a time. Named after the PID
    // form each one runs: AutoPositional -> PositionalPid, AutoVelocityEquation
    // -> VelocityEquationPid (see docs/servo-control-strategies.md).
    enum class Channel : uint8_t { None = 0, AutoPositional = 1, Manual = 2, AutoVelocityEquation = 3 };

    inline const char *channelName(Channel c)
    {
        switch (c)
        {
        case Channel::None:                 return "NONE";
        case Channel::AutoPositional:       return "AUTO_POSITIONAL";
        case Channel::Manual:               return "MANUAL";
        case Channel::AutoVelocityEquation: return "AUTO_VELOCITYEQUATION";
        }
        return "?";
    }

    // Short press on MODE advances NONE -> AUTO_POSITIONAL -> MANUAL ->
    // AUTO_VELOCITYEQUATION -> NONE. AUTO_VELOCITYEQUATION is appended last so
    // it doesn't shift the existing NONE/AUTO_POSITIONAL/MANUAL muscle memory.
    inline Channel nextChannel(Channel c)
    {
        switch (c)
        {
        case Channel::None:                 return Channel::AutoPositional;
        case Channel::AutoPositional:       return Channel::Manual;
        case Channel::Manual:               return Channel::AutoVelocityEquation;
        case Channel::AutoVelocityEquation: return Channel::None;
        }
        return Channel::None;
    }

    // Bumped whenever ConfigBlob's layout or semantics change. A stored blob
    // with a different version is rejected and the compiled defaults reloaded -
    // a stale blob is never reinterpreted.
    //
    // v4: pan_pos_gains/tilt_pos_gains (direct-position, unit-space Kp~0.1)
    // became pan_veq_gains/tilt_veq_gains (velocity-equation, degree-space
    // Kp~1-5) - same layout, incompatible semantics, so an old blob's gains
    // must not be reinterpreted under the new algorithm.
    constexpr uint16_t SCHEMA_VERSION = 4;

    // The persisted configuration. POD and trivially copyable: written to NVS
    // as one blob and copied out under the config mutex by ctrl each step.
    struct ConfigBlob
    {
        uint8_t input_channel; // Channel

        Gains pan_gains, tilt_gains;         // rate-output PID, PositionalPid (AutoPositionalChannel)
        Gains pan_veq_gains, tilt_veq_gains; // velocity-equation PID, VelocityEquationPid (AutoVelocityEquationChannel)

        Zone zone;

        uint8_t laser_brightness;      // 0..100, Phase 1 PWM - stored only in Phase 0
        uint8_t telemetry_rate_hz;     // 1..50
        uint8_t log_sd_enabled;        // 0/1
        uint8_t telemetry_wifi_enabled; // 0/1
        uint8_t telemetry_ble_enabled;  // 0/1
        uint8_t boot_tour;              // 0/1 - run ZONE_TOUR after SELFTEST
    };

    // Safe defaults: transmission off, logging on, channel NONE, laser off.
    constexpr ConfigBlob CONFIG_DEFAULTS = {
        /* input_channel          */ (uint8_t)Channel::None,
        /* pan_gains              */ {PAN_KP, PAN_KI, PAN_KD},
        /* tilt_gains             */ {TILT_KP, TILT_KI, TILT_KD},
        /* pan_veq_gains          */ {PAN_VEQ_KP, PAN_VEQ_KI, PAN_VEQ_KD},
        /* tilt_veq_gains         */ {TILT_VEQ_KP, TILT_VEQ_KI, TILT_VEQ_KD},
        /* zone                   */ {WORK_PAN_MIN, WORK_PAN_MAX, WORK_TILT_MIN, WORK_TILT_MAX},
        /* laser_brightness       */ 0,
        /* telemetry_rate_hz      */ 10,
        /* log_sd_enabled         */ 1,
        /* telemetry_wifi_enabled */ 0,
        /* telemetry_ble_enabled  */ 0,
        /* boot_tour              */ 0,
    };

    // Field bounds for validation.
    constexpr float GAIN_MIN = 0.0f;
    constexpr float GAIN_MAX = 1000.0f;
    constexpr uint8_t TELEMETRY_RATE_MIN = 1;
    constexpr uint8_t TELEMETRY_RATE_MAX = 50;
    constexpr uint8_t LASER_BRIGHTNESS_MAX = 100;
}
