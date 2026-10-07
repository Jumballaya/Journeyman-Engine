#pragma once

#include <atomic>
#include <cstdio>
#include <string>
#include <string_view>

#include "LogBook.hpp"

// Talking to `jm` through a shell: quoting its arguments, and reading its output.
namespace shell {

// One argument for sh: always single-quoted.
std::string quotePosix(std::string_view arg);
// One argument for cmd.exe, as the C runtime (and Go's os.Args) split it:
// double-quoted, with backslashes before a quote doubled and quotes written
// as "" (which also keeps cmd's own quote tracking in step).
std::string quoteWindows(std::string_view arg);
// This platform's.
std::string quote(std::string_view arg);

// A shell command running in the background, its output (stdout and stderr)
// read line by line. cancel() ends it and everything it started, from any
// thread; destroying a running one cancels it.
class Process {
 public:
  Process() = default;
  ~Process();
  Process(const Process&) = delete;
  Process& operator=(const Process&) = delete;

  // Runs `command` through sh (cmd.exe on Windows). False if it couldn't start.
  bool start(const std::string& command);
  // The next line, without its newline; false once the output has ended.
  bool readLine(std::string& line);
  // Waits for it to end: its exit status, -1 if it didn't exit normally (cancelled, crashed).
  int wait();
  void cancel();
  bool cancelled() const { return _cancelled; }

 private:
  struct Native;
  Native* _native = nullptr;
  std::FILE* _output = nullptr;
  std::atomic<bool> _cancelled{false};
};

// How a line of jm output reads: an error, a warning or neither. Only for
// showing it; whether the command failed is its exit status.
LogBook::Level levelOf(std::string_view line);

}  // namespace shell
