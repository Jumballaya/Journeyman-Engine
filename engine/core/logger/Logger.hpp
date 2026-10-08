#pragma once

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <functional>
#include <string>

enum class LogLevel { Trace, Debug, Info, Warn, Error, Critical };

// Where an error came from, when whoever reports it knows (JM_REPORT_ERROR):
// a project path, and a 1-based line and column when there is one.
struct ErrorSource {
  std::string file;
  int line = 0;
  int column = 0;
};

class Logger {
 public:
  explicit Logger(const std::string& loggerName, const std::string& logFilePath);
  void log(LogLevel level, std::string_view message, const ErrorSource* source = nullptr);
  void flush();
  // Called after every Error and Critical message is logged, with its source
  // if it has one: how a run reports its errors to tools (JM_ERRORS).
  using ErrorListener = std::function<void(LogLevel, std::string_view message, const ErrorSource&)>;
  void setErrorListener(ErrorListener listener) { _errorListener = std::move(listener); }
  // Another destination for every message (an editor's console).
  void addSink(spdlog::sink_ptr sink) { _logger->sinks().push_back(std::move(sink)); }

 private:
  std::shared_ptr<spdlog::logger> _logger;
  ErrorListener _errorListener;
};
