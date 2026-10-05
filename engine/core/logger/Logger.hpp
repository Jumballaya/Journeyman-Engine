#pragma once

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <string>

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
  // Another destination for every message (an editor's console).
  void addSink(spdlog::sink_ptr sink) { _logger->sinks().push_back(std::move(sink)); }

 private:
  std::shared_ptr<spdlog::logger> _logger;
  static spdlog::level::level_enum convertLevel(LogLevel level);
};
