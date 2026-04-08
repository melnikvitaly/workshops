#pragma once
#include <Arduino.h>
class DebugOut
{
private:
    String scope;

public:
    DebugOut(const String &scope);
    void print(const String &debugStr);
    DebugOut& Scoped(const String &scope);
    void print(const int value);
    template <typename T>
    void append(const T &str) {
        Serial.print(str);
    }
};