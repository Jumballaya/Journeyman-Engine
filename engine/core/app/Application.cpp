#include "Application.hpp"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <iostream>
#include <memory>
#include <string_view>

#include "../assets/Archive.hpp"
#include "../ecs/component/SchemaJson.hpp"
#include "../logger/logging.hpp"
#include "Engine.hpp"
#include "Platform.hpp"

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
  // Every component's scene JSON and script fields, as JSON on stdout: the
  // schema tools read. Needs no project, window or GL.
  if (_argc > 1 && std::string_view(_argv[1]) == "--schema") {
    Engine engine(".", std::string(kManifestEntryKey));
    engine.registerComponents();
    std::cout << schemaJson(engine.getWorld().getComponentRegistry()).dump(2) << "\n";
    return 0;
  }

  const std::filesystem::path bundled = _argc > 1 ? std::filesystem::path{} : findBundledArchive();
  auto logger = makeLogger(!bundled.empty());
  if (!logger) {
    std::cerr << "Journeyman: could not open any log file\n";
    return 1;
  }
  LoggerService::initialize(std::move(logger));
  JM_LOG_INFO("Journeyman Engine Starting up...");

  std::filesystem::path input = std::string(kManifestEntryKey);
  if (_argc > 1) {
    input = _argv[1];
  } else if (!bundled.empty()) {
    input = bundled;
  }

  // An archive (.jm, or a game executable) or a game folder holds its manifest
  // under kManifestEntryKey; a .json path is the manifest itself.
  std::filesystem::path rootDir = input;
  std::filesystem::path manifestPath = std::string(kManifestEntryKey);
  if (input.extension() == ".jm" || input == bundled) {
    if (!std::filesystem::is_regular_file(input)) {
      JM_LOG_ERROR("[Archive] not a regular file: {}", input.string());
      return 1;
    }
  } else if (input.extension() == ".json") {
    rootDir = input.parent_path();
    manifestPath = input;
  } else if (!std::filesystem::is_directory(input)) {
    JM_LOG_ERROR("Unknown input type. Must be a .json, directory, or .jm archive.");
    return 1;
  }
  JM_LOG_INFO("Mounting '{}', manifest '{}'", rootDir.string(), manifestPath.string());

  // An escaped exception (startup, or mid-game) is reported, not an abort().
  try {
    Engine engine(rootDir, manifestPath);
    engine.initialize();
    engine.run();
  } catch (const std::exception& e) {
    JM_LOG_CRITICAL("Fatal: {}", e.what());
    LoggerService::instance().flush();
    std::cerr << "Journeyman: " << e.what() << "\n";
    return 1;
  }
  JM_LOG_INFO("Journeyman Engine Shut Down");
  LoggerService::instance().flush();
  return 0;
}
