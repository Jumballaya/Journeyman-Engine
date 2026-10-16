#include "Platform.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <string>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#elif defined(_WIN32)
#include <windows.h>
#endif
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace platform {

std::filesystem::path executablePath() {
#if defined(__APPLE__)
  char buf[PATH_MAX];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) != 0) return {};
  std::error_code ec;
  auto p = std::filesystem::weakly_canonical(buf, ec);
  return ec ? std::filesystem::path(buf) : p;
#elif defined(_WIN32)
  wchar_t buf[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (n == 0) return {};
  return std::filesystem::path(buf);
#else
  std::error_code ec;
  auto p = std::filesystem::read_symlink("/proc/self/exe", ec);
  return ec ? std::filesystem::path{} : p;
#endif
}

namespace {
std::string sanitize(std::string_view name) {
  std::string out;
  for (char c : name) {
    bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '-' || c == '_' || c == '.';
    out += ok ? c : '_';
  }
  return out.empty() ? std::string("JourneymanGame") : out;
}
const char* env(const char* name) {
  const char* v = std::getenv(name);
  return (v && *v) ? v : nullptr;
}
}  // namespace

std::filesystem::path userDataDir(std::string_view gameName) {
  const std::string game = sanitize(gameName);
#if defined(__APPLE__)
  if (const char* home = env("HOME")) {
    return std::filesystem::path(home) / "Library" / "Application Support" / game;
  }
#elif defined(_WIN32)
  if (const char* appData = env("APPDATA")) return std::filesystem::path(appData) / game;
#else
  if (const char* xdg = env("XDG_DATA_HOME")) return std::filesystem::path(xdg) / game;
  if (const char* home = env("HOME")) return std::filesystem::path(home) / ".local" / "share" / game;
#endif
  return std::filesystem::temp_directory_path() / game;
}

namespace {
// std::filesystem::rename may not replace an existing file on every Windows toolchain.
void replaceFile(const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& ec) {
  ec.clear();
#if defined(_WIN32)
  if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    ec.assign(static_cast<int>(GetLastError()), std::system_category());
  }
#else
  std::filesystem::rename(from, to, ec);
#endif
}
}  // namespace

namespace {
// Where link's chain of symlinks ends, existing or not (still a link: a loop).
std::filesystem::path linkTarget(const std::filesystem::path& link) {
  std::error_code ec;
  auto target = link;
  for (int hops = 0; hops < 40 && std::filesystem::is_symlink(target, ec); ++hops) {
    const auto next = std::filesystem::read_symlink(target, ec);
    if (next.is_absolute()) {
      target = next;
    } else {  // relative to the link's real folder, which Windows won't find from "alias\.."
      const auto dir = target.has_parent_path() ? target.parent_path() : std::filesystem::path(".");
      const auto real = std::filesystem::weakly_canonical(dir, ec);
      target = (ec ? dir : real) / next;
    }
  }
  return target;
}

// The lock file for path: beside the file its links lead to, in that folder's real place (as jm's filelock.Hold).
std::filesystem::path lockFileFor(const std::filesystem::path& path) {
  const auto target = linkTarget(path);
  std::error_code ec;
  const auto dir = target.has_parent_path() ? target.parent_path() : std::filesystem::path(".");
  std::filesystem::create_directories(dir, ec);  // as a write there would
  const auto real = std::filesystem::weakly_canonical(dir, ec);
  return (ec ? dir : real) / ("." + target.filename().string() + ".lock");
}
}  // namespace

bool writeAtomically(const std::filesystem::path& link, std::string_view bytes, std::string& error) {
  std::error_code ec;
  const auto target = linkTarget(link);
  if (std::filesystem::is_symlink(target, ec)) {  // a loop: give up, like the OS does
    error = "Too many levels of symbolic links at " + link.string();
    return false;
  }
  const auto kept = std::filesystem::status(target, ec).permissions();
  // Hidden, so folder scans skip it; random, so writers in other processes don't share it.
  const auto temp = target.parent_path() / ("." + target.filename().string() + ".tmp-" + std::to_string(std::random_device{}()));
  if (!target.parent_path().empty()) std::filesystem::create_directories(target.parent_path(), ec);
  std::ofstream out(temp, std::ios::binary | std::ios::trunc);
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  out.close();
  if (kept != std::filesystem::perms::unknown) std::filesystem::permissions(temp, kept, ec);
  if (out.fail()) {
    error = "Couldn't write " + target.string();
  } else if (replaceFile(temp, target, ec); ec) {
    error = "Couldn't replace " + target.string() + ": " + ec.message();
  } else {
    return true;
  }
  std::filesystem::remove(temp, ec);
  return false;
}

#if defined(_WIN32)
FileLock::FileLock(const std::filesystem::path& path)
    : _handle(CreateFileW(lockFileFor(path).c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr)) {
  OVERLAPPED at{};  // every byte, as jm's lock_windows.go
  if (_handle != INVALID_HANDLE_VALUE && !LockFileEx(_handle, LOCKFILE_EXCLUSIVE_LOCK, 0, MAXDWORD, MAXDWORD, &at)) {
    CloseHandle(_handle);
    _handle = INVALID_HANDLE_VALUE;
  }
}

FileLock::~FileLock() {
  if (held()) CloseHandle(_handle);  // unlocks it
}

bool FileLock::held() const { return _handle != INVALID_HANDLE_VALUE; }
#else
FileLock::FileLock(const std::filesystem::path& path) : _fd(open(lockFileFor(path).c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644)) {
  int locked = -1;
  while (_fd >= 0 && (locked = flock(_fd, LOCK_EX)) != 0 && errno == EINTR) {
  }
  if (_fd >= 0 && locked != 0) {
    close(_fd);
    _fd = -1;
  }
}

FileLock::~FileLock() {
  if (held()) close(_fd);  // unlocks it
}

bool FileLock::held() const { return _fd >= 0; }
#endif

#if defined(_WIN32)
namespace {
// Points a CRT stream at the console unless it's already a pipe or a file.
void toConsole(DWORD which, const char* device, const char* mode, FILE* stream) {
  const DWORD type = GetFileType(GetStdHandle(which));
  if (type == FILE_TYPE_PIPE || type == FILE_TYPE_DISK) return;
  FILE* reopened = nullptr;
  freopen_s(&reopened, device, mode, stream);
}
}  // namespace
#endif

void attachParentConsole() {
#if defined(_WIN32)
  // Attached even when everything is redirected, so Ctrl+C in the terminal reaches it.
  if (!AttachConsole(ATTACH_PARENT_PROCESS)) return;
  toConsole(STD_INPUT_HANDLE, "CONIN$", "r", stdin);
  toConsole(STD_OUTPUT_HANDLE, "CONOUT$", "w", stdout);
  toConsole(STD_ERROR_HANDLE, "CONOUT$", "w", stderr);
#endif
}

}  // namespace platform
