#pragma once

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <string>
#include <string_view>

#include "ILogger.hpp"

class SpdLogger : public ILogger {
 public:
  explicit SpdLogger(const std::string& loggerName, const std::string& logFilePath);
  void log(LogLevel level, std::string_view message) override;
  void flush() override;

 private:
  std::shared_ptr<spdlog::logger> _logger;
  static spdlog::level::level_enum convertLevel(LogLevel level);
};
