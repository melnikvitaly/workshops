#include "DebugOut.h"
constexpr char *DELIM = "|";
DebugOut::DebugOut(const String &scope) : scope(scope + DELIM) {}

void DebugOut::print(const String &str)
{
    Serial.print(millis());
    Serial.print(DELIM);
    Serial.print(scope);
    Serial.println(str);
}

DebugOut &DebugOut::Scoped(const String &scope)
{
    return *(new DebugOut(scope));
}
