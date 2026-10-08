#pragma once

#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include "../logger/Logger.hpp"

// A run's errors for tools (JM_ERRORS, JM_STRICT): each one a JSON line,
//   {"level": "error", "category": "Script", "message": "...",
//    "file": "assets/scripts/hero.ts", "line": 12, "column": 5, "frame": 240}
// where category is the message's [Tag], and file/line/column appear when the
// reporter knew them (JM_REPORT_ERROR). "frame" is the frame it happened in
// (0: while starting).
class ErrorReport {
 public:
  // out: "-" for stderr, a file path, or empty to write nothing (count only).
  explicit ErrorReport(const std::string& out);

  static nlohmann::json toJson(LogLevel level, std::string_view message, const ErrorSource& source, uint64_t frame);
  void add(LogLevel level, std::string_view message, const ErrorSource& source, uint64_t frame);
  size_t count() const { return _count; }
  const std::string& first() const { return _first; }

 private:
  std::unique_ptr<std::ofstream> _file;
  bool _stderr = false;
  size_t _count = 0;
  std::string _first;  // the first error's message
};
