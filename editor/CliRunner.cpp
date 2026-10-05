#include "CliRunner.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "LogBook.hpp"
#include "core/app/Platform.hpp"

namespace fs = std::filesystem;

namespace {

double now() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::string shellQuote(const std::string& s) {
#ifdef _WIN32
  return "\"" + s + "\"";
#else
  std::string out = "'";
  for (char c : s) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return out + "'";
#endif
}

bool isExecutable(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

LogBook::Level levelOf(const std::string& line) {
  std::string lower = line;
  for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (lower.find("error") != std::string::npos || lower.find("failed") != std::string::npos) return LogBook::Level::Error;
  if (lower.find("warning") != std::string::npos) return LogBook::Level::Warning;
  return LogBook::Level::Info;
}

}  // namespace

CliRunner::~CliRunner() {
  if (_thread.joinable()) _thread.join();
}

fs::path CliRunner::locate() {
#ifdef _WIN32
  const char* exe = "jm.exe";
#else
  const char* exe = "jm";
#endif
  if (const char* env = std::getenv("JM_CLI"); env && isExecutable(env)) return env;
  const fs::path here = platform::executableDir();
  for (const fs::path& candidate : {here / exe, here / ".." / "bin" / exe, here / ".." / ".." / "bin" / exe,
                                    here / ".." / ".." / ".." / "build" / "bin" / exe}) {
    if (isExecutable(candidate)) return fs::weakly_canonical(candidate);
  }
  if (const char* path = std::getenv("PATH")) {
#ifdef _WIN32
    const char sep = ';';
#else
    const char sep = ':';
#endif
    std::string all = path;
    for (size_t start = 0; start <= all.size();) {
      const size_t end = std::min(all.find(sep, start), all.size());
      if (end > start && isExecutable(fs::path(all.substr(start, end - start)) / exe)) {
        return fs::path(all.substr(start, end - start)) / exe;
      }
      start = end + 1;
    }
  }
  return {};
}

bool CliRunner::start(const fs::path& cwd, const std::vector<std::string>& args, std::string label) {
  if (_busy) return false;
  const fs::path jm = locate();
  if (jm.empty()) {
    LogBook::instance().add(LogBook::Level::Error, LogBook::Source::Build,
                            "Can't find the jm CLI: put it beside the editor, on PATH, or set JM_CLI.");
    std::lock_guard lock(_mutex);
    _finished = Finished{label, false, 0, "jm CLI not found"};
    return false;
  }
  if (_thread.joinable()) _thread.join();

  // jm finds the engine (journeyman_engine) on PATH: put the editor's and jm's folders first.
#ifdef _WIN32
  std::string command = "cd /d " + shellQuote(cwd.string()) + " && ";  // /d: also across drives
#else
  std::string command = "cd " + shellQuote(cwd.string()) + " && ";
#endif
#ifndef _WIN32
  // (A packaged editor has it beside itself; a dev build in build/<preset>/engine/.)
  const fs::path here = platform::executableDir();
  command += "PATH=" + shellQuote(here.string() + ":" + (here.parent_path() / "engine").string() + ":" + jm.parent_path().string()) +
             ":\"$PATH\" ";
#endif
  command += shellQuote(jm.string());
  for (const auto& arg : args) command += " " + shellQuote(arg);
  command += " 2>&1";

  _busy = true;
  _label = label;
  _startTime = now();
  LogBook::instance().add(LogBook::Level::Info, LogBook::Source::Build, "$ jm " + [&] {
    std::string joined;
    for (const auto& a : args) joined += (joined.empty() ? "" : " ") + a;
    return joined;
  }());
  _thread = std::thread([this, command, label = std::move(label)]() {
#ifdef _WIN32
    FILE* pipe = _popen(command.c_str(), "r");
#else
    FILE* pipe = popen(command.c_str(), "r");
#endif
    bool sawError = false;
    std::string last;
    if (pipe) {
      char buffer[4096];
      while (std::fgets(buffer, sizeof(buffer), pipe)) {
        std::string line = buffer;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
        if (line.empty()) continue;
        const auto level = levelOf(line);
        sawError |= level == LogBook::Level::Error;
        LogBook::instance().add(level, LogBook::Source::Build, line);
        last = line;
        std::lock_guard lock(_mutex);
        _lastLine = line;
      }
    }
#ifdef _WIN32
    const int status = pipe ? _pclose(pipe) : -1;
#else
    const int status = pipe ? pclose(pipe) : -1;
#endif
    const bool ok = status == 0 && !sawError;
    {
      std::lock_guard lock(_mutex);
      _finished = Finished{label, ok, now() - _startTime, last};
    }
    _busy = false;
  });
  return true;
}

double CliRunner::elapsed() const { return _busy ? now() - _startTime : 0.0; }

std::string CliRunner::lastLine() const {
  std::lock_guard lock(_mutex);
  return _lastLine;
}

std::optional<CliRunner::Finished> CliRunner::takeFinished() {
  std::lock_guard lock(_mutex);
  auto out = std::move(_finished);
  _finished.reset();
  return out;
}
