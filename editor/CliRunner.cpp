#include "CliRunner.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <regex>
#include <sstream>

#include "LogBook.hpp"
#include "Shell.hpp"
#include "core/app/Platform.hpp"

namespace fs = std::filesystem;

namespace {

double now() {
  return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

// The PATH a terminal would have. Apps started from Finder or a launcher get
// a bare system PATH without Node (Homebrew, nvm, Volta...), so ask the
// user's login shell once, and add the usual install places as a fallback.
std::string userPath() {
#ifdef _WIN32
  const char* path = std::getenv("PATH");
  return path ? path : "";
#else
  static const std::string cached = [] {
    std::string path;
    const char* shell = std::getenv("SHELL");
    const std::string command = std::string(shell && *shell ? shell : "/bin/zsh") +
                                " -ilc 'printf \"\\n__JM_PATH__%s\" \"$PATH\"' 2>/dev/null </dev/null";
    if (FILE* pipe = popen(command.c_str(), "r")) {
      std::string output;
      char buffer[4096];
      while (std::fgets(buffer, sizeof(buffer), pipe)) output += buffer;
      pclose(pipe);
      // Shell start-up files may print; the marker finds our line.
      if (const size_t at = output.rfind("__JM_PATH__"); at != std::string::npos) {
        path = output.substr(at + 11);
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r')) path.pop_back();
      }
    }
    const char* inherited = std::getenv("PATH");
    const char* home = std::getenv("HOME");
    std::string extra = "/opt/homebrew/bin:/usr/local/bin";
    if (home) extra += std::string(":") + home + "/.volta/bin:" + home + "/.local/bin";
    return path + ":" + extra + ":" + (inherited ? inherited : "/usr/bin:/bin");
  }();
  return cached;
#endif
}

bool isFile(const fs::path& p) {
  std::error_code ec;
  return fs::is_regular_file(p, ec);
}

}  // namespace

CliRunner::~CliRunner() {
  if (_busy) cancel();
  if (_thread.joinable()) _thread.join();
}

void CliRunner::cancel() {
  if (_busy && _process) _process->cancel();
}

fs::path CliRunner::locate() {
#ifdef _WIN32
  const char* exe = "jm.exe";
#else
  const char* exe = "jm";
#endif
  if (const char* env = std::getenv("JM_CLI"); env && isFile(env)) return env;
  const fs::path here = platform::executableDir();
  for (const fs::path& candidate : {here / exe, here / ".." / "bin" / exe, here / ".." / ".." / "bin" / exe,
                                    here / ".." / ".." / ".." / "build" / "bin" / exe}) {
    if (isFile(candidate)) return fs::weakly_canonical(candidate);
  }
#ifdef _WIN32
  const char sep = ';';
#else
  const char sep = ':';
#endif
  std::istringstream dirs(std::getenv("PATH") ? std::getenv("PATH") : "");
  for (std::string dir; std::getline(dirs, dir, sep);) {
    if (!dir.empty() && isFile(fs::path(dir) / exe)) return fs::path(dir) / exe;
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
    _finished = Finished{label, false, false, 0, "jm CLI not found"};
    return false;
  }
  if (_thread.joinable()) _thread.join();
  _process = std::make_unique<shell::Process>();

#ifdef _WIN32
  std::string command = "cd /d " + shell::quote(cwd.string()) + " && ";  // /d: also across drives
#else
  // jm finds the engine (journeyman_engine) on PATH: put the editor's and jm's folders first.
  // (A packaged editor has it beside itself; a dev build in build/<preset>/engine/.)
  const fs::path here = platform::executableDir();
  std::string command = "cd " + shell::quote(cwd.string()) + " && PATH=" +
                        shell::quote(here.string() + ":" + (here.parent_path() / "engine").string() + ":" +
                                   jm.parent_path().string() + ":" + userPath()) + " ";
#endif
  command += shell::quote(jm.string());
  for (const auto& arg : args) command += " " + shell::quote(arg);
  command += " 2>&1";

  _busy = true;
  _label = label;
  _startTime = now();
  {
    std::lock_guard lock(_mutex);
    _lastLine.clear();  // not the previous job's
  }
  std::string shown = "$ jm";
  for (const auto& arg : args) shown += " " + arg;
  LogBook::instance().add(LogBook::Level::Info, LogBook::Source::Build, shown);
  // Started here, not on the thread, so a cancel() right after always has a process to stop.
  const bool started = _process->start(command);
  _thread = std::thread([this, started, label = std::move(label)]() {
    shell::Process& process = *_process;
    for (std::string line; started && process.readLine(line);) {
      if (line.empty()) continue;
      // AssemblyScript names an error's place on a later line, relative to the scripts folder:
      //   └─ in lib/db.ts(42,7)
      static const std::regex where(R"(in ([\w./-]+\.ts)\((\d+),\d+\))");
      std::smatch m;
      if (line.find("\xE2\x94\x94") != std::string::npos && std::regex_search(line, m, where)) {
        std::string file = m[1].str();
        while (file.starts_with("../")) file = file.substr(3);
        LogBook::instance().locateLastError("assets/scripts/" + file, std::atoi(m[2].str().c_str()));
        continue;
      }
      LogBook::instance().add(shell::levelOf(line), LogBook::Source::Build, line);
      std::lock_guard lock(_mutex);
      _lastLine = line;
    }
    const bool ok = started && process.wait() == 0;  // jm exits non-zero on any failure
    if (process.cancelled()) LogBook::instance().add(LogBook::Level::Warning, LogBook::Source::Build, label + " cancelled");
    {
      std::lock_guard lock(_mutex);
      _finished = Finished{label, ok, process.cancelled(), now() - _startTime, process.cancelled() ? "Cancelled" : _lastLine};
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
