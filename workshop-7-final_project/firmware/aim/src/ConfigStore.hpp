#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <nvs.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "Config.hpp"

// The configuration plane (docs/architecture.md §5, docs/protocol.md §3.3).
//
// One versioned, flat key space. Precedence: compiled defaults -> NVS -> runtime
// set(). Every write runs the one path: validate -> apply -> persist -> the
// caller acknowledges with a cfg.state line.
//
// The blob is guarded by a mutex created in app_main. ctrl never calls set();
// it copies the whole struct out with snapshot() at the top of each step and
// releases immediately. Writers (link_uart, ui) hold the mutex only for the
// validate+apply+persist, none of which runs on the control task.
class ConfigStore
{
public:
    struct Value
    {
        enum class Type : uint8_t { Number, Bool, String } type = Type::Number;
        double num  = 0.0;
        bool   flag = false;
        char   str[16] = {0};

        static Value number(double n) { Value v; v.type = Type::Number; v.num = n; return v; }
        static Value boolean(bool b)  { Value v; v.type = Type::Bool;   v.flag = b; return v; }
        static Value string(const char *s)
        {
            Value v;
            v.type = Type::String;
            std::strncpy(v.str, s, sizeof(v.str) - 1);
            return v;
        }
    };

    // err is nullptr on success, otherwise one of the docs/protocol.md §3.3
    // reasons: "range", "type", "unknown_key", "readonly", "nvs_write", "schema".
    struct Result
    {
        bool        ok;
        const char *err;
    };

    // Open NVS and load the blob. On a missing or version-mismatched blob the
    // compiled defaults are written back. Returns true if defaults were used
    // (so app_main can note it in the boot event). Runs before any task starts.
    //
    // Two mutexes on purpose: `mutex` (from app_main) guards the in-RAM blob and
    // is the only lock ctrl's snapshot() ever contends on - held only for a
    // struct copy, never across NVS. `_nvsMutex` serialises the NVS handle
    // between the two writers (link_uart, ui) and is where a commit's latency
    // lands, off the control path.
    bool load(SemaphoreHandle_t mutex)
    {
        _mutex    = mutex;
        _nvsMutex = xSemaphoreCreateMutexStatic(&_nvsMutexBuf);
        ESP_ERROR_CHECK(nvs_open(NAMESPACE, NVS_READWRITE, &_nvs));

        uint16_t ver = 0;
        config::ConfigBlob stored{};
        size_t   len = sizeof(stored);

        const esp_err_t verErr  = nvs_get_u16(_nvs, KEY_VER, &ver);
        const esp_err_t blobErr = nvs_get_blob(_nvs, KEY_BLOB, &stored, &len);

        if (verErr != ESP_OK || blobErr != ESP_OK ||
            len != sizeof(stored) || ver != config::SCHEMA_VERSION)
        {
            ESP_LOGW(TAG, "no usable config in NVS (ver=%u, want %u) - loading defaults",
                     ver, config::SCHEMA_VERSION);
            _blob = config::CONFIG_DEFAULTS;
            writeNvs(_blob);
            return true;
        }

        _blob = stored;
        ESP_LOGI(TAG, "config loaded from NVS, schema v%u", ver);
        return false;
    }

    void snapshot(config::ConfigBlob &out)
    {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        out = _blob;
        xSemaphoreGive(_mutex);
    }

    config::Channel channel()
    {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        const auto c = (config::Channel)_blob.input_channel;
        xSemaphoreGive(_mutex);
        return c;
    }

    Result set(const char *key, const Value &v)
    {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        const config::ConfigBlob prev = _blob;
        const char             *err  = apply(key, v);
        const config::ConfigBlob next = _blob;
        if (err)
            _blob = prev;
        xSemaphoreGive(_mutex);

        if (err)
            return {false, err};

        if (writeNvs(next) != ESP_OK)
        {
            xSemaphoreTake(_mutex, portMAX_DELAY);
            _blob = prev; // best-effort rollback; nvs_write failure is rare
            xSemaphoreGive(_mutex);
            return {false, "nvs_write"};
        }
        return {true, nullptr};
    }

    Result reset()
    {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _blob = config::CONFIG_DEFAULTS;
        const config::ConfigBlob next = _blob;
        xSemaphoreGive(_mutex);

        return (writeNvs(next) == ESP_OK) ? Result{true, nullptr}
                                          : Result{false, "nvs_write"};
    }

    // Format the current value of `key` for a cfg.state `v` field. Unknown keys
    // yield "null".
    void format(const char *key, char *out, size_t cap)
    {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        formatLocked(key, out, cap);
        xSemaphoreGive(_mutex);
    }

private:
    static constexpr char TAG[]       = "CFG";
    static constexpr char NAMESPACE[] = "aimcfg";
    static constexpr char KEY_BLOB[]  = "blob";
    static constexpr char KEY_VER[]   = "ver";

    SemaphoreHandle_t  _mutex    = nullptr; // guards _blob (from app_main)
    SemaphoreHandle_t  _nvsMutex = nullptr; // serialises the NVS handle
    StaticSemaphore_t  _nvsMutexBuf{};
    nvs_handle_t       _nvs  = 0;
    config::ConfigBlob _blob = config::CONFIG_DEFAULTS;

    // Persist a blob snapshot. Runs off the config mutex, so a commit's flash
    // latency never lands on ctrl's snapshot(). `_nvsMutex` is null only during
    // load(), which is single-threaded.
    esp_err_t writeNvs(const config::ConfigBlob &blob)
    {
        if (_nvsMutex)
            xSemaphoreTake(_nvsMutex, portMAX_DELAY);
        esp_err_t e = nvs_set_u16(_nvs, KEY_VER, config::SCHEMA_VERSION);
        if (e == ESP_OK)
            e = nvs_set_blob(_nvs, KEY_BLOB, &blob, sizeof(blob));
        if (e == ESP_OK)
            e = nvs_commit(_nvs);
        if (_nvsMutex)
            xSemaphoreGive(_nvsMutex);
        return e;
    }

    // --- validation + apply, mutex already held -----------------------------

    static const char *checkGain(const Value &v, float &dst)
    {
        if (v.type != Value::Type::Number)
            return "type";
        if (!std::isfinite(v.num) || v.num < config::GAIN_MIN || v.num > config::GAIN_MAX)
            return "range";
        dst = (float)v.num;
        return nullptr;
    }

    static const char *checkBool(const Value &v, uint8_t &dst)
    {
        if (v.type == Value::Type::Bool)
        {
            dst = v.flag ? 1 : 0;
            return nullptr;
        }
        if (v.type == Value::Type::Number && (v.num == 0.0 || v.num == 1.0))
        {
            dst = (uint8_t)v.num;
            return nullptr;
        }
        return (v.type == Value::Type::Number) ? "range" : "type";
    }

    const char *checkZone(const Value &v, float lo, float hi, float other, bool isMin, float &dst)
    {
        if (v.type != Value::Type::Number)
            return "type";
        if (!std::isfinite(v.num) || v.num < lo || v.num > hi)
            return "range";
        if (isMin && (float)v.num >= other)
            return "range";
        if (!isMin && (float)v.num <= other)
            return "range";
        dst = (float)v.num;
        return nullptr;
    }

    const char *apply(const char *key, const Value &v)
    {
        if (!std::strcmp(key, "input.channel"))
        {
            if (v.type != Value::Type::String)
                return "type";
            if (!std::strcmp(v.str, "NONE"))   { _blob.input_channel = (uint8_t)config::Channel::None;   return nullptr; }
            if (!std::strcmp(v.str, "AUTO"))   { _blob.input_channel = (uint8_t)config::Channel::Auto;   return nullptr; }
            if (!std::strcmp(v.str, "MANUAL")) { _blob.input_channel = (uint8_t)config::Channel::Manual; return nullptr; }
            return "range";
        }

        if (!std::strcmp(key, "pid.pan.kp"))  return checkGain(v, _blob.pid_pan_kp);
        if (!std::strcmp(key, "pid.pan.ki"))  return checkGain(v, _blob.pid_pan_ki);
        if (!std::strcmp(key, "pid.pan.kd"))  return checkGain(v, _blob.pid_pan_kd);
        if (!std::strcmp(key, "pid.tilt.kp")) return checkGain(v, _blob.pid_tilt_kp);
        if (!std::strcmp(key, "pid.tilt.ki")) return checkGain(v, _blob.pid_tilt_ki);
        if (!std::strcmp(key, "pid.tilt.kd")) return checkGain(v, _blob.pid_tilt_kd);

        if (!std::strcmp(key, "zone.pan.min"))
            return checkZone(v, config::GIMBAL_PAN_MIN, config::GIMBAL_PAN_MAX, _blob.zone_pan_max, true, _blob.zone_pan_min);
        if (!std::strcmp(key, "zone.pan.max"))
            return checkZone(v, config::GIMBAL_PAN_MIN, config::GIMBAL_PAN_MAX, _blob.zone_pan_min, false, _blob.zone_pan_max);
        if (!std::strcmp(key, "zone.tilt.min"))
            return checkZone(v, config::GIMBAL_TILT_MIN, config::GIMBAL_TILT_MAX, _blob.zone_tilt_max, true, _blob.zone_tilt_min);
        if (!std::strcmp(key, "zone.tilt.max"))
            return checkZone(v, config::GIMBAL_TILT_MIN, config::GIMBAL_TILT_MAX, _blob.zone_tilt_min, false, _blob.zone_tilt_max);

        if (!std::strcmp(key, "laser.brightness"))
        {
            if (v.type != Value::Type::Number)
                return "type";
            if (!std::isfinite(v.num) || v.num < 0.0 || v.num > config::LASER_BRIGHTNESS_MAX)
                return "range";
            _blob.laser_brightness = (uint8_t)v.num;
            return nullptr;
        }

        if (!std::strcmp(key, "telemetry.rate_hz"))
        {
            if (v.type != Value::Type::Number)
                return "type";
            if (!std::isfinite(v.num) || v.num < config::TELEMETRY_RATE_MIN || v.num > config::TELEMETRY_RATE_MAX)
                return "range";
            _blob.telemetry_rate_hz = (uint8_t)v.num;
            return nullptr;
        }

        if (!std::strcmp(key, "log.sd.enabled"))        return checkBool(v, _blob.log_sd_enabled);
        if (!std::strcmp(key, "telemetry.wifi.enabled")) return checkBool(v, _blob.telemetry_wifi_enabled);
        if (!std::strcmp(key, "telemetry.ble.enabled"))  return checkBool(v, _blob.telemetry_ble_enabled);

        return "unknown_key";
    }

    void formatLocked(const char *key, char *out, size_t cap)
    {
        if (!std::strcmp(key, "input.channel"))
        {
            std::snprintf(out, cap, "\"%s\"", config::channelName((config::Channel)_blob.input_channel));
            return;
        }
        if (!std::strcmp(key, "pid.pan.kp"))  { fmtNum(out, cap, _blob.pid_pan_kp);  return; }
        if (!std::strcmp(key, "pid.pan.ki"))  { fmtNum(out, cap, _blob.pid_pan_ki);  return; }
        if (!std::strcmp(key, "pid.pan.kd"))  { fmtNum(out, cap, _blob.pid_pan_kd);  return; }
        if (!std::strcmp(key, "pid.tilt.kp")) { fmtNum(out, cap, _blob.pid_tilt_kp); return; }
        if (!std::strcmp(key, "pid.tilt.ki")) { fmtNum(out, cap, _blob.pid_tilt_ki); return; }
        if (!std::strcmp(key, "pid.tilt.kd")) { fmtNum(out, cap, _blob.pid_tilt_kd); return; }
        if (!std::strcmp(key, "zone.pan.min"))  { fmtNum(out, cap, _blob.zone_pan_min);  return; }
        if (!std::strcmp(key, "zone.pan.max"))  { fmtNum(out, cap, _blob.zone_pan_max);  return; }
        if (!std::strcmp(key, "zone.tilt.min")) { fmtNum(out, cap, _blob.zone_tilt_min); return; }
        if (!std::strcmp(key, "zone.tilt.max")) { fmtNum(out, cap, _blob.zone_tilt_max); return; }
        if (!std::strcmp(key, "laser.brightness"))  { std::snprintf(out, cap, "%u", _blob.laser_brightness);  return; }
        if (!std::strcmp(key, "telemetry.rate_hz")) { std::snprintf(out, cap, "%u", _blob.telemetry_rate_hz); return; }
        if (!std::strcmp(key, "log.sd.enabled"))        { fmtBool(out, cap, _blob.log_sd_enabled);        return; }
        if (!std::strcmp(key, "telemetry.wifi.enabled")) { fmtBool(out, cap, _blob.telemetry_wifi_enabled); return; }
        if (!std::strcmp(key, "telemetry.ble.enabled"))  { fmtBool(out, cap, _blob.telemetry_ble_enabled);  return; }
        std::snprintf(out, cap, "null");
    }

    static void fmtNum(char *out, size_t cap, float v) { std::snprintf(out, cap, "%g", (double)v); }
    static void fmtBool(char *out, size_t cap, uint8_t v) { std::snprintf(out, cap, v ? "true" : "false"); }
};
