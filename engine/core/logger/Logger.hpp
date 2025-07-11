#pragma once

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <string>
#include <string_view>

enum class LogLevel {
  Trace,
  Debug,
  Info,
  Warn,
  Error,
  Critical
};

class Logger {
 public:
  explicit Logger(const std::string& loggerName, const std::string& logFilePath);
  void log(LogLevel level, std::string_view message);
  void flush();

 private:
  std::shared_ptr<spdlog::logger> _logger;
  static spdlog::level::level_enum convertLevel(LogLevel level);
};
