#pragma once

#include <memory>

#include "Logger.hpp"

class LoggerService {
 public:
  static void initialize(std::unique_ptr<Logger> logger);
  static Logger& instance();

 private:
  LoggerService() = default;
  static LoggerService& get();

  static std::unique_ptr<Logger> _logger;
};