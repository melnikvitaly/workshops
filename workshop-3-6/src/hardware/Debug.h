#pragma once
#include <cstdio>
#include <string>
#include <esp_timer.h>

#define ALL_LEVEL 0
#define INHERIT_LEVEL -1
#define NONE_LEVEL 99

class Debug
{
  static constexpr const char *DELIM = "|";

  std::string scope;
  int level;

public:
  Debug(const std::string &scope, int level = ALL_LEVEL)
      : scope(scope + DELIM), level(level) {}

  void print(const std::string &str)
  {
    if (level > 0)
      return;
    printf("%lu%s%s%s\n",
           (uint32_t)(esp_timer_get_time() / 1000),
           DELIM, scope.c_str(), str.c_str());
  }

  void print(int value) { print(std::to_string(value)); }

  Debug &Scoped(const std::string &subscope, int lvl = INHERIT_LEVEL)
  {
    lvl = lvl < 0 ? level : lvl;
    return *(new Debug(scope + subscope, lvl));
  }

  template <typename T>
  void append(const T &val) { printf("%d", (int)val); }

  template <typename T>
  void dumpChange(const std::string &what, T from, T to)
  {
    print(what + ": " + std::to_string((int)from) + "=>" + std::to_string((int)to));
  }
};
