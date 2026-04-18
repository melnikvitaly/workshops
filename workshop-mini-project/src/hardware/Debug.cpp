#include "Debug.h"
constexpr const char *DELIM = "|";
Debug::Debug(const String &scope, int level) : scope(scope + DELIM),
                                                     level(level)
{
}

void Debug::print(const String &str)
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

void Debug::print(const int value)
{
    if (level > 0)
    {
        return;
    }
    print(String(value));
}

Debug &Debug::Scoped(const String &scope, int level)
{
    level = level < 0 ? this->level : level;
    return *(new Debug(scope, level)); // terrible with dynamic creation, maybe ok for static objects,
}
