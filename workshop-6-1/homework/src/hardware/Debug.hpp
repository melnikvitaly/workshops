#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <esp_log.h>

// Scoped logger over ESP_LOG. Held by value (not by reference as in 2-3) so
// Scoped() no longer leaks a heap Debug per call.
class Debug
{
    static constexpr size_t TAG_LEN = 32;
    static constexpr size_t MSG_LEN = 128;

    char _tag[TAG_LEN];

public:
    explicit Debug(const char *scope) { snprintf(_tag, sizeof(_tag), "%s", scope); }

    Debug Scoped(const char *subscope) const
    {
        Debug        child(_tag);
        const size_t len = strlen(child._tag);
        if (len + 1 < TAG_LEN)
            snprintf(child._tag + len, TAG_LEN - len, "|%s", subscope);
        return child;
    }

    void print(const char *fmt, ...) const __attribute__((format(printf, 2, 3)))
    {
        char msg[MSG_LEN];
        va_list args;
        va_start(args, fmt);
        vsnprintf(msg, sizeof(msg), fmt, args);
        va_end(args);
        ESP_LOGI(_tag, "%s", msg);
    }

    void dumpChange(const char *what, int from, int to) const
    {
        print("%s: %d=>%d", what, from, to);
    }

    const char *tag() const { return _tag; }
};
