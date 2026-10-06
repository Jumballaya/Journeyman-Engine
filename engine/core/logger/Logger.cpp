#include "Logger.hpp"

// LogLevel mirrors spdlog's levels, so converting is a cast.
static_assert(static_cast<int>(LogLevel::Trace) == spdlog::level::trace &&
              static_cast<int>(LogLevel::Info) == spdlog::level::info &&
              static_cast<int>(LogLevel::Critical) == spdlog::level::critical);

Logger::Logger(const std::string& loggerName, const std::string& logFilePath) {
  _logger = spdlog::basic_logger_mt(loggerName, logFilePath);
  _logger->set_level(spdlog::level::trace);
  _logger->flush_on(spdlog::level::warn);
}

void Logger::log(LogLevel level, std::string_view message) {
  _logger->log(static_cast<spdlog::level::level_enum>(level), message);
}

void Logger::flush() { _logger->flush(); }
