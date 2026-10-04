#include "LoggerService.hpp"

#include <utility>

std::unique_ptr<Logger> LoggerService::_logger = nullptr;

void LoggerService::initialize(std::unique_ptr<Logger> logger) {
  get()._logger = std::move(logger);
}

Logger& LoggerService::instance() {
  return *(get()._logger);
}

LoggerService& LoggerService::get() {
  static LoggerService instance;
  return instance;
}