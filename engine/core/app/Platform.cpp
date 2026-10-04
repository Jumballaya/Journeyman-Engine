#include "Platform.hpp"

#include <cstdlib>
#include <string>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#elif defined(_WIN32)
#include <windows.h>
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

}  // namespace platform
