#pragma once
#include <Arduino.h>
#define ALL_LEVEL 0
#define INHERIT_LEVEL -1
#define NONE_LEVEL 99
class DebugOut
{
private:
    String scope;
    int level;

public:
    DebugOut(const String &scope, int level = ALL_LEVEL);
    void print(const String &debugStr);
    DebugOut &Scoped(const String &scope, int level = INHERIT_LEVEL);
    void print(const int value);
    template <typename T>
    void append(const T &str)
    {
        Serial.print(str);
    }
};