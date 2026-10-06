#pragma once

#include <spdlog/spdlog.h>

#include "LoggerService.hpp"

// 0 trace .. 5 critical: levels below it compile to nothing.
#ifndef JM_LOG_LEVEL
#define JM_LOG_LEVEL 2
#endif

// do/while keeps `if (x) JM_LOG_INFO(...); else ...` binding the else to the caller's if.
#define JM_LOG_AT(n, level, msg, ...)                                                              \
  do {                                                                                             \
    if constexpr (JM_LOG_LEVEL <= n)                                                               \
      LoggerService::instance().log(LogLevel::level, spdlog::fmt_lib::format(msg, ##__VA_ARGS__)); \
  } while (0)

#define JM_LOG_TRACE(...) JM_LOG_AT(0, Trace, __VA_ARGS__)
#define JM_LOG_DEBUG(...) JM_LOG_AT(1, Debug, __VA_ARGS__)
#define JM_LOG_INFO(...) JM_LOG_AT(2, Info, __VA_ARGS__)
#define JM_LOG_WARN(...) JM_LOG_AT(3, Warn, __VA_ARGS__)
#define JM_LOG_ERROR(...) JM_LOG_AT(4, Error, __VA_ARGS__)
#define JM_LOG_CRITICAL(...) JM_LOG_AT(5, Critical, __VA_ARGS__)
