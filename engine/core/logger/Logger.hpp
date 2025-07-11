#pragma once

#include <memory>

#include "ILogger.hpp"

class Logger {
 public:
  static void initialize(std::unique_ptr<ILogger> logger);
  static ILogger& instance();

 private:
  Logger() = default;
  static Logger& get();

  static std::unique_ptr<ILogger> _logger;
};