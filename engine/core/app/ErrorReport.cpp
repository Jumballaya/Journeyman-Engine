#include "ErrorReport.hpp"

#include <iostream>

ErrorReport::ErrorReport(const std::string& out) {
  if (out == "-") {
    _stderr = true;
  } else if (!out.empty()) {
    _file = std::make_unique<std::ofstream>(out, std::ios::trunc);
    if (!*_file) std::cerr << "Journeyman: can't write JM_ERRORS file " << out << "\n";
  }
}

nlohmann::json ErrorReport::toJson(LogLevel level, std::string_view message, const ErrorSource& source, uint64_t frame) {
  nlohmann::json out = {{"level", level == LogLevel::Critical ? "critical" : "error"}};
  // "[Script] hero.ts trapped..." -> category "Script", message "hero.ts trapped..."
  std::string text(message);
  if (text.starts_with('[')) {
    if (const size_t close = text.find(']'); close != std::string::npos) {
      out["category"] = text.substr(1, close - 1);
      text = text.substr(close + 1);
      if (text.starts_with(' ')) text.erase(0, 1);
    }
  }
  out["message"] = text;
  if (!source.file.empty()) out["file"] = source.file;
  if (source.line > 0) out["line"] = source.line;
  if (source.column > 0) out["column"] = source.column;
  out["frame"] = frame;
  return out;
}

void ErrorReport::add(LogLevel level, std::string_view message, const ErrorSource& source, uint64_t frame) {
  if (_count++ == 0) _first = message;
  if (!_stderr && !_file) return;
  const std::string line = toJson(level, message, source, frame).dump();
  if (_stderr) std::cerr << line << std::endl;
  if (_file) *_file << line << std::endl;
}
