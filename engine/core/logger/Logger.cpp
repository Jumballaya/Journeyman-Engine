#include "Logger.hpp"

#include <utility>

std::unique_ptr<ILogger> Logger::_logger = nullptr;

void Logger::initialize(std::unique_ptr<ILogger> logger) {
  get()._logger = std::move(logger);
}

ILogger& Logger::instance() {
  return *(get()._logger);
}

//
//  Hold a static reference to the logger instance
//  and then later run initialize to move-assign
//  an ILogger into the static instance's _logger
//
Logger& Logger::get() {
  static Logger instance;
  return instance;
}