#include "LoggerService.hpp"

#include <utility>

std::unique_ptr<Logger> LoggerService::_logger = nullptr;

void LoggerService::initialize(std::unique_ptr<Logger> logger) {
  get()._logger = std::move(logger);
}

Logger& LoggerService::instance() {
  return *(get()._logger);
}

//
//  Hold a static reference to the logger instance
//  and then later run initialize to move-assign
//  a Logger into the static instance's _logger
//
LoggerService& LoggerService::get() {
  static LoggerService instance;
  return instance;
}