#include "Shell.hpp"

#include <cerrno>
#include <regex>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#else
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace shell {

std::string quotePosix(std::string_view arg) {
  std::string out = "'";
  for (char c : arg) out += c == '\'' ? std::string("'\\''") : std::string(1, c);
  return out + "'";
}

std::string quoteWindows(std::string_view arg) {
  std::string out = "\"";
  size_t backslashes = 0;
  for (char c : arg) {
    if (c == '\\') {
      ++backslashes;
      continue;
    }
    // Backslashes count only before a quote, where each must be doubled.
    out.append(c == '"' ? backslashes * 2 : backslashes, '\\');
    backslashes = 0;
    out += c == '"' ? std::string("\"\"") : std::string(1, c);
  }
  out.append(backslashes * 2, '\\');  // before the closing quote
  return out + "\"";
}

std::string quote(std::string_view arg) {
#ifdef _WIN32
  return quoteWindows(arg);
#else
  return quotePosix(arg);
#endif
}

#ifdef _WIN32

// The process and a job holding it and its children, so cancel ends them all.
struct Process::Native {
  HANDLE process = nullptr;
  HANDLE job = nullptr;
};

bool Process::start(const std::string& command) {
  SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
  HANDLE readEnd = nullptr, writeEnd = nullptr;
  if (!CreatePipe(&readEnd, &writeEnd, &inherit, 0)) return false;
  SetHandleInformation(readEnd, HANDLE_FLAG_INHERIT, 0);  // only the child's end crosses over

  STARTUPINFOA startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdOutput = writeEnd;
  startup.hStdError = writeEnd;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION info{};
  std::string line = "cmd.exe /c " + command;
  const bool started = CreateProcessA(nullptr, line.data(), nullptr, nullptr, TRUE,
                                      CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &startup, &info);
  CloseHandle(writeEnd);
  if (!started) {
    CloseHandle(readEnd);
    return false;
  }
  _native = new Native{info.hProcess, CreateJobObjectA(nullptr, nullptr)};
  if (_native->job) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(_native->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    AssignProcessToJobObject(_native->job, info.hProcess);
  }
  ResumeThread(info.hThread);
  CloseHandle(info.hThread);
  _output = _fdopen(_open_osfhandle(reinterpret_cast<intptr_t>(readEnd), _O_RDONLY), "r");
  return true;
}

int Process::wait() {
  if (!_native) return -1;
  WaitForSingleObject(_native->process, INFINITE);
  DWORD code = 1;
  GetExitCodeProcess(_native->process, &code);
  return _cancelled ? -1 : static_cast<int>(code);
}

void Process::cancel() {
  _cancelled = true;
  if (_native && _native->job) TerminateJobObject(_native->job, 1);
  else if (_native) TerminateProcess(_native->process, 1);
}

Process::~Process() {
  if (_native) {
    cancel();
    wait();
    CloseHandle(_native->process);
    if (_native->job) CloseHandle(_native->job);
    delete _native;
  }
  if (_output) std::fclose(_output);
}

#else

// The child leads its own process group, so cancel can signal it and every
// process it started (jm's node, npm, asc).
struct Process::Native {
  pid_t pid = 0;
  std::atomic<bool> reaped{false};  // after which the pid may belong to someone else
  int status = -1;
};

bool Process::start(const std::string& command) {
  int fds[2];
  if (pipe(fds) != 0) return false;
  posix_spawn_file_actions_t actions;
  posix_spawn_file_actions_init(&actions);
  posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
  posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
  posix_spawn_file_actions_addclose(&actions, fds[0]);
  posix_spawn_file_actions_addclose(&actions, fds[1]);
  posix_spawnattr_t attributes;
  posix_spawnattr_init(&attributes);
  posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
  posix_spawnattr_setpgroup(&attributes, 0);  // a new group, numbered by the child's pid

  std::string program = "/bin/sh", flag = "-c", script = command;
  char* argv[] = {program.data(), flag.data(), script.data(), nullptr};
  pid_t pid = 0;
  const int failed = posix_spawn(&pid, "/bin/sh", &actions, &attributes, argv, environ);
  posix_spawn_file_actions_destroy(&actions);
  posix_spawnattr_destroy(&attributes);
  close(fds[1]);
  if (failed) {
    close(fds[0]);
    return false;
  }
  _native = new Native;
  _native->pid = pid;
  _output = fdopen(fds[0], "r");
  return true;
}

int Process::wait() {
  if (!_native) return -1;
  if (!_native->reaped) {
    int status = 0;
    while (waitpid(_native->pid, &status, 0) < 0 && errno == EINTR) {
    }
    _native->reaped = true;
    _native->status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  }
  return _cancelled ? -1 : _native->status;
}

void Process::cancel() {
  _cancelled = true;
  if (_native && !_native->reaped) kill(-_native->pid, SIGTERM);
}

Process::~Process() {
  if (_native) {
    if (!_native->reaped) {
      cancel();
      wait();
    }
    delete _native;
  }
  if (_output) std::fclose(_output);
}

#endif

bool Process::readLine(std::string& line) {
  line.clear();
  if (!_output) return false;
  char buffer[4096];
  while (std::fgets(buffer, sizeof(buffer), _output)) {
    line += buffer;
    if (line.back() == '\n') break;  // else a long line: keep reading it
  }
  if (line.empty()) return false;
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
  return true;
}

LogBook::Level levelOf(std::string_view line) {
  // Whole words, so "error_screen.ui.html" and "0 errors" don't count.
  static const std::regex error(R"((^|[^\w])(error|errors|failed|failure|fatal)\b)", std::regex::icase);
  static const std::regex none(R"(\b(0|no) errors\b)", std::regex::icase);
  static const std::regex warning(R"((^|[^\w])(warning|warn)\b)", std::regex::icase);
  const std::string text(line);
  if (std::regex_search(text, error) && !std::regex_search(text, none)) return LogBook::Level::Error;
  if (std::regex_search(text, warning)) return LogBook::Level::Warning;
  return LogBook::Level::Info;
}

}  // namespace shell
