#pragma once
#include <cstdint>
#include <driver/ledc.h>
#include <utils/ViewPort.hpp>

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
    constexpr float SERVO_PAN_MAX_RATE = 150.0f;
    constexpr float SERVO_TILT_MAX_RATE = 120.0f;

    // LEDC resource assignment for the two servo channels.
    constexpr ledc_channel_t PAN_PWM_CHANNEL = LEDC_CHANNEL_0;
    constexpr ledc_timer_t PAN_PWM_TIMER = LEDC_TIMER_0;
    constexpr ledc_channel_t TILT_PWM_CHANNEL = LEDC_CHANNEL_1;
    constexpr ledc_timer_t TILT_PWM_TIMER = LEDC_TIMER_1;

    // The buzzer/Beeper from workshop-5 is dropped: interfaces.md §1 has no pin
    // for it, and OLED + status LED are the documented feedback path.

    // --- Gimbal travel limits (degrees) --------------------------------------
    // Hard mechanical stops. The gimbal never commands outside these.
    constexpr float GIMBAL_PAN_MIN = 15.0f;
    constexpr float GIMBAL_PAN_MAX = 105.0f; // was 100: raised to let the working
                                             // zone reach 5 deg further LEFT.
                                             // NOT re-measured on the rig - confirm
                                             // the arm actually has travel here.
    constexpr float GIMBAL_TILT_MIN = 46.0f;
    constexpr float GIMBAL_TILT_MAX = 125.0f;

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

    // --- Boot zone tour ------------------------------------------------------
    // At startup the laser walks the perimeter of the working zone, so the
    // operator can see where it can reach before the loop takes over. It also
    // shows which way each axis moves, which is the thing you most want to know
    // before raising the gains.
    constexpr bool ZONE_TOUR_AT_BOOT = true;
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
    constexpr float PAN_MAX_SLEW = 120.0f;
    constexpr float TILT_MAX_SLEW = 90.0f;

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
    constexpr uint32_t UI_DEBOUNCE_MS  = 30;   // docs/architecture.md §4
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

    // --- Queue depths --------------------------------------------------------
    constexpr int CMD_Q_LEN = 24;
    constexpr int LOG_Q_LEN = 64; // deep, drop-oldest with a counter

    // --- SD logging (docs/architecture.md §5, docs/interfaces.md §4) ---------
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

    // --- Config plane ------------------------------------------------------
    // Exactly one input channel is processed at a time (docs/architecture.md §4).
    enum class Channel : uint8_t { None = 0, Auto = 1, Manual = 2 };

    inline const char *channelName(Channel c)
    {
        switch (c)
        {
        case Channel::None:   return "NONE";
        case Channel::Auto:   return "AUTO";
        case Channel::Manual: return "MANUAL";
        }
        return "?";
    }

    // Short press on MODE advances NONE -> AUTO -> MANUAL -> NONE.
    inline Channel nextChannel(Channel c)
    {
        switch (c)
        {
        case Channel::None:   return Channel::Auto;
        case Channel::Auto:   return Channel::Manual;
        case Channel::Manual: return Channel::None;
        }
        return Channel::None;
    }

    // Bumped whenever ConfigBlob's layout or semantics change. A stored blob
    // with a different version is rejected and the compiled defaults reloaded -
    // a stale blob is never reinterpreted.
    constexpr uint16_t SCHEMA_VERSION = 1;

    // The persisted configuration. POD and trivially copyable: written to NVS
    // as one blob and copied out under the config mutex by ctrl each step.
    struct ConfigBlob
    {
        uint8_t input_channel; // Channel

        float pid_pan_kp, pid_pan_ki, pid_pan_kd;
        float pid_tilt_kp, pid_tilt_ki, pid_tilt_kd;

        float zone_pan_min, zone_pan_max;
        float zone_tilt_min, zone_tilt_max;

        uint8_t laser_brightness;      // 0..100, Phase 1 PWM - stored only in Phase 0
        uint8_t telemetry_rate_hz;     // 1..50
        uint8_t log_sd_enabled;        // 0/1
        uint8_t telemetry_wifi_enabled; // 0/1
        uint8_t telemetry_ble_enabled;  // 0/1
    };

    // Safe defaults (docs/architecture.md §5): transmission off, logging on,
    // channel NONE, laser off.
    constexpr ConfigBlob CONFIG_DEFAULTS = {
        /* input_channel          */ (uint8_t)Channel::None,
        /* pid_pan_{kp,ki,kd}     */ PAN_KP, PAN_KI, PAN_KD,
        /* pid_tilt_{kp,ki,kd}    */ TILT_KP, TILT_KI, TILT_KD,
        /* zone_pan_{min,max}     */ WORK_PAN_MIN, WORK_PAN_MAX,
        /* zone_tilt_{min,max}    */ WORK_TILT_MIN, WORK_TILT_MAX,
        /* laser_brightness       */ 0,
        /* telemetry_rate_hz      */ 10,
        /* log_sd_enabled         */ 1,
        /* telemetry_wifi_enabled */ 0,
        /* telemetry_ble_enabled  */ 0,
    };

    // Field bounds for validation. Gains per docs/protocol.md §2.3.
    constexpr float GAIN_MIN = 0.0f;
    constexpr float GAIN_MAX = 1000.0f;
    constexpr uint8_t TELEMETRY_RATE_MIN = 1;
    constexpr uint8_t TELEMETRY_RATE_MAX = 50;
    constexpr uint8_t LASER_BRIGHTNESS_MAX = 100;
}
