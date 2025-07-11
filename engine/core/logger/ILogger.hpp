#pragma once

#include <string_view>

enum class LogLevel {
  Trace,
  Debug,
  Info,
  Warn,
  Error,
  Critical
};

class ILogger {
 public:
  virtual ~ILogger() = default;
  virtual void log(LogLevel level, std::string_view message) = 0;
  virtual void flush() = 0;
};