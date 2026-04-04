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
};