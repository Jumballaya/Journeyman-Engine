#include "SpdLogger.hpp"

SpdLogger::SpdLogger(const std::string& loggerName, const std::string& logFilePath) {
  _logger = spdlog::basic_logger_mt(loggerName, logFilePath);
  _logger->set_level(spdlog::level::trace);
  _logger->flush_on(spdlog::level::warn);
}

void SpdLogger::log(LogLevel level, std::string_view message) {
  _logger->log(convertLevel(level), message);
}

spdlog::level::level_enum SpdLogger::convertLevel(LogLevel level) {
  switch (level) {
    case LogLevel::Trace:
      return spdlog::level::trace;
    case LogLevel::Debug:
      return spdlog::level::debug;
    case LogLevel::Info:
      return spdlog::level::info;
    case LogLevel::Warn:
      return spdlog::level::warn;
    case LogLevel::Error:
      return spdlog::level::err;
    case LogLevel::Critical:
      return spdlog::level::critical;
    default:
      return spdlog::level::info;
  }
}

void SpdLogger::flush() {
  _logger->flush();
}
