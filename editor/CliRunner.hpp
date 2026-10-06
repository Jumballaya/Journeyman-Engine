#pragma once

#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

// Runs the jm CLI (build, export) in the background, one job at a time, its
// output streaming into the console as Build lines.
class CliRunner {
 public:
  struct Finished {
    std::string label;  // "Build"
    bool ok = false;
    double seconds = 0;
    std::string lastLine;  // the summary or the error
  };

  ~CliRunner();

  // The jm executable: $JM_CLI, beside the editor, the repo's build/bin, or PATH. Empty if none.
  static std::filesystem::path locate();

  // False when a job is running or jm can't be found (the reason goes to the console).
  bool start(const std::filesystem::path& cwd, const std::vector<std::string>& args, std::string label);
  bool busy() const { return _busy; }
  const std::string& label() const { return _label; }
  double elapsed() const;
  std::string lastLine() const;

  // The job that ended since the last call, once.
  std::optional<Finished> takeFinished();

 private:
  std::thread _thread;
  std::atomic<bool> _busy{false};
  std::string _label;
  double _startTime = 0;
  mutable std::mutex _mutex;
  std::string _lastLine;
  std::optional<Finished> _finished;
};
