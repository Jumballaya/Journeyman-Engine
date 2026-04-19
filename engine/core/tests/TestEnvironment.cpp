#include <filesystem>
#include <memory>
#include <string>

#include "Logger.hpp"
#include "LoggerService.hpp"

// Static initializer: runs before main(), which runs before any GoogleTest
// case. Ensures JM_LOG_* macros in engine code don't dereference a null
// Logger during tests that exercise error paths.
namespace {
struct LoggerInit {
  LoggerInit() {
    auto logPath = std::filesystem::temp_directory_path() / "jm_test.log";
    LoggerService::initialize(std::make_unique<Logger>("jm_test", logPath.string()));
  }
};
[[maybe_unused]] const LoggerInit g_loggerInit;
}  // namespace
