#pragma once
#include <Arduino.h>

#define ALL_LEVEL 0
#define INHERIT_LEVEL -1
#define NONE_LEVEL 99

#define _NAME(x) #x
class Debug
{
  static constexpr const char *DELIM = "|";

  String scope;
  int level;

public:
  Debug(const String &scope, int level = ALL_LEVEL)
      : scope(scope + DELIM), level(level) {}

  void print(const String &str)
  {
    if (level > 0)
      return;
    Serial.print(millis());
    Serial.print(DELIM);
    Serial.print(scope);
    Serial.println(str);
  }

  void print(const int value) { print(String(value)); }

  Debug &Scoped(const String &subscope, int lvl = INHERIT_LEVEL)
  {
    lvl = lvl < 0 ? level : lvl;
    return *(new Debug(scope + subscope, lvl));
  }

  template <typename T>
  void append(const T &val) { Serial.print(val); }

  template <typename T>
  void dumpChange(String what, T from, T to)
  {
    print(what + String(": ") + (int)from + String("=>") + (int)to);
  }
};
