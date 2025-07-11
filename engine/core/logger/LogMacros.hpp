#pragma once

#include <spdlog/spdlog.h>

#include "LoggerService.hpp"

#ifndef JM_LOG_LEVEL
#define JM_LOG_LEVEL 2
#endif

#define JM_LOG_TRACE(msg, ...) \
  if constexpr (JM_LOG_LEVEL <= 0) LoggerService::instance().log(LogLevel::Trace, spdlog::fmt_lib::format(msg, ##__VA_ARGS__))

#define JM_LOG_DEBUG(msg, ...) \
  if constexpr (JM_LOG_LEVEL <= 1) LoggerService::instance().log(LogLevel::Debug, spdlog::fmt_lib::format(msg, ##__VA_ARGS__))

#define JM_LOG_INFO(msg, ...) \
  if constexpr (JM_LOG_LEVEL <= 2) LoggerService::instance().log(LogLevel::Info, spdlog::fmt_lib::format(msg, ##__VA_ARGS__))

#define JM_LOG_WARN(msg, ...) \
  if constexpr (JM_LOG_LEVEL <= 3) LoggerService::instance().log(LogLevel::Warn, spdlog::fmt_lib::format(msg, ##__VA_ARGS__))

#define JM_LOG_ERROR(msg, ...) \
  if constexpr (JM_LOG_LEVEL <= 4) LoggerService::instance().log(LogLevel::Error, spdlog::fmt_lib::format(msg, ##__VA_ARGS__))

#define JM_LOG_CRITICAL(msg, ...) \
  if constexpr (JM_LOG_LEVEL <= 5) LoggerService::instance().log(LogLevel::Critical, spdlog::fmt_lib::format(msg, ##__VA_ARGS__))

// above/below: Blank lines for macro
