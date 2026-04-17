#include "DebugOut.h"
constexpr const char *DELIM = "|";
DebugOut::DebugOut(const String &scope, int level) : scope(scope + DELIM),
                                                     level(level)
{
}

void DebugOut::print(const String &str)
{
    if (level > 0)
    {
        return;
    }

    Serial.print(millis());
    Serial.print(DELIM);
    Serial.print(scope);
    Serial.println(str);
}

void DebugOut::print(const int value)
{
    if (level > 0)
    {
        return;
    }
    print(String(value));
}

DebugOut &DebugOut::Scoped(const String &scope, int level)
{
    level = level < 0 ? this->level : level;
    return *(new DebugOut(scope, level));
}
