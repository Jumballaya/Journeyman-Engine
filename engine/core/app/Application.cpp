#include "Application.hpp"

#include <filesystem>
#include <memory>

#include "../logger/logging.hpp"
#include "Engine.hpp"

Application::Application(int argc, char** argv) : _argc(argc), _argv(argv) {}

Application::~Application() = default;

int Application::run() {
  LoggerService::initialize(std::make_unique<Logger>("engine", "logs/engine.log"));

  JM_LOG_INFO("Journeyman Engine Starting up...");
  JM_LOG_DEBUG("Debug logging active!");

  std::filesystem::path rootPath = ".jm.json";
  if (_argc > 1) {
    rootPath = _argv[1];
  }

  std::filesystem::path rootDir;
  std::filesystem::path manifestPath;

  // Running bundled archive (not implemented yet)
  if (rootPath.extension() == ".jm") {
    JM_LOG_ERROR("[Archive] Archive mode not yet implemented");
    return 1;
  }
  // Running bundled game from config file directly
  if (rootPath.extension() == ".json") {
    rootDir = rootPath.parent_path();
    manifestPath = rootPath;
    JM_LOG_INFO("[JSON] Mounting: {}", rootDir.string());
    JM_LOG_INFO("[JSON] Manifest: {}", manifestPath.string());
  }
  // Running bundled game from game folder; look for a ".jm.json" inside
  else if (std::filesystem::is_directory(rootPath)) {
    rootDir = rootPath;
    manifestPath = ".jm.json";
    JM_LOG_INFO("[JSON] Mounting: {}", rootDir.string());
    JM_LOG_INFO("[JSON] Manifest: {}", manifestPath.string());
  } else {
    JM_LOG_ERROR("Unknown input type. Must be a .json, directory, or .jm archive.");
    return 1;
  }

  _engine = std::make_unique<Engine>(rootDir, manifestPath);
  _engine->initialize();
  _engine->run();

  JM_LOG_INFO("Journeyman Engine Shut Down");
  return 0;
}
