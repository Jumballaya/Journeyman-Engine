#pragma once

#include <memory>

#include "Logger.hpp"

// The process-wide Logger behind the JM_LOG_* macros; initialize it before logging.
class LoggerService {
 public:
  static void initialize(std::unique_ptr<Logger> logger) { _logger = std::move(logger); }
  static Logger& instance() { return *_logger; }

 private:
  static inline std::unique_ptr<Logger> _logger;
};
