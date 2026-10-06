#include "CliRunner.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <regex>
#include <sstream>

#include "LogBook.hpp"
#include "core/app/Platform.hpp"

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

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
    _finished = Finished{label, false, 0, "jm CLI not found"};
    return false;
  }
  if (_thread.joinable()) _thread.join();

#ifdef _WIN32
  std::string command = "cd /d " + shellQuote(cwd.string()) + " && ";  // /d: also across drives
#else
  // jm finds the engine (journeyman_engine) on PATH: put the editor's and jm's folders first.
  // (A packaged editor has it beside itself; a dev build in build/<preset>/engine/.)
  const fs::path here = platform::executableDir();
  std::string command = "cd " + shellQuote(cwd.string()) + " && PATH=" +
                        shellQuote(here.string() + ":" + (here.parent_path() / "engine").string() + ":" +
                                   jm.parent_path().string() + ":" + userPath()) + " ";
#endif
  command += shellQuote(jm.string());
  for (const auto& arg : args) command += " " + shellQuote(arg);
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
  _thread = std::thread([this, command, label = std::move(label)]() {
    FILE* pipe = popen(command.c_str(), "r");
    bool sawError = false;
    if (pipe) {
      char buffer[4096];
      while (std::fgets(buffer, sizeof(buffer), pipe)) {
        std::string line = buffer;
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
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
        const auto level = levelOf(line);
        sawError |= level == LogBook::Level::Error;
        LogBook::instance().add(level, LogBook::Source::Build, line);
        std::lock_guard lock(_mutex);
        _lastLine = line;
      }
    }
    const bool ok = (pipe ? pclose(pipe) : -1) == 0 && !sawError;
    {
      std::lock_guard lock(_mutex);
      _finished = Finished{label, ok, now() - _startTime, _lastLine};
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
