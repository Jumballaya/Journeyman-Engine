#include "Application.hpp"

#include <filesystem>
#include <memory>

#include "../assets/Archive.hpp"
#include "../logger/logging.hpp"
#include "Engine.hpp"
#include "Platform.hpp"

#include <iostream>
#include <spdlog/spdlog.h>

Application::Application(int argc, char** argv) : _argc(argc), _argv(argv) {}

Application::~Application() = default;

namespace {

// An exported game carries its archive inside the executable, or (older
// exports) ships game.jm beside it or in the app bundle's Resources folder.
// Empty if there is none.
std::filesystem::path findBundledArchive() {
  if (const auto exe = platform::executablePath(); !exe.empty() && Archive::isEmbeddedIn(exe)) return exe;
  const auto exeDir = platform::executableDir();
  if (exeDir.empty()) return {};
  for (const auto& candidate : {exeDir / "game.jm", exeDir / ".." / "Resources" / "game.jm"}) {
    if (std::filesystem::is_regular_file(candidate)) return candidate.lexically_normal();
  }
  return {};
}

// Dev runs log to ./logs; a standalone game logs into its per-user data dir,
// named after the executable (jm export names it after the game).
std::unique_ptr<Logger> makeLogger(bool standalone) {
  std::vector<std::filesystem::path> candidates;
  if (!standalone) candidates.push_back("logs/engine.log");
  candidates.push_back(platform::userDataDir(platform::executablePath().stem().string()) / "logs" / "engine.log");
  candidates.push_back(std::filesystem::temp_directory_path() / "journeyman" / "engine.log");
  for (const auto& path : candidates) {
    try {
      return std::make_unique<Logger>("engine", path.string());
    } catch (const std::exception&) {
      spdlog::drop("engine");
    }
  }
  return nullptr;
}

}  // namespace

int Application::run() {
  std::filesystem::path bundled = _argc > 1 ? std::filesystem::path{} : findBundledArchive();
  auto logger = makeLogger(!bundled.empty());
  if (!logger) {
    std::cerr << "Journeyman: could not open any log file\n";
    return 1;
  }
  LoggerService::initialize(std::move(logger));

  JM_LOG_INFO("Journeyman Engine Starting up...");
  JM_LOG_DEBUG("Debug logging active!");

  std::filesystem::path rootPath = std::string(kManifestEntryKey);
  if (_argc > 1) {
    rootPath = _argv[1];
  } else if (!bundled.empty()) {
    rootPath = bundled;
  }

  std::filesystem::path rootDir;
  std::filesystem::path manifestPath;

  // A .jm path (or a game executable) mounts the archive; its manifest is stored under kManifestEntryKey.
  if (rootPath.extension() == ".jm" || rootPath == bundled) {
    if (!std::filesystem::is_regular_file(rootPath)) {
      JM_LOG_ERROR("[Archive] not a regular file: {}", rootPath.string());
      return 1;
    }
    rootDir = rootPath;
    manifestPath = std::string(kManifestEntryKey);
    JM_LOG_INFO("[Archive] Mounting: {}", rootDir.string());
    JM_LOG_INFO("[Archive] Manifest: {}", manifestPath.string());
  }
  // Running bundled game from config file directly
  else if (rootPath.extension() == ".json") {
    rootDir = rootPath.parent_path();
    manifestPath = rootPath;
    JM_LOG_INFO("[JSON] Mounting: {}", rootDir.string());
    JM_LOG_INFO("[JSON] Manifest: {}", manifestPath.string());
  }
  // Running bundled game from game folder; look for the manifest inside.
  else if (std::filesystem::is_directory(rootPath)) {
    rootDir = rootPath;
    manifestPath = std::string(kManifestEntryKey);
    JM_LOG_INFO("[JSON] Mounting: {}", rootDir.string());
    JM_LOG_INFO("[JSON] Manifest: {}", manifestPath.string());
  } else {
    JM_LOG_ERROR("Unknown input type. Must be a .json, directory, or .jm archive.");
    return 1;
  }

  try {
    _engine = std::make_unique<Engine>(rootDir, manifestPath);
    _engine->initialize();
  } catch (const std::exception& e) {
    JM_LOG_CRITICAL("Startup failed: {}", e.what());
    LoggerService::instance().flush();
    std::cerr << "Journeyman: startup failed: " << e.what() << "\n";
    return 1;
  }
  _engine->run();
  LoggerService::instance().flush();

  JM_LOG_INFO("Journeyman Engine Shut Down");
  return 0;
}
