#include "Application.hpp"

#include <filesystem>
#include <memory>

#include "../assets/Archive.hpp"
#include "../logger/logging.hpp"
#include "Engine.hpp"

Application::Application(int argc, char** argv) : _argc(argc), _argv(argv) {}

Application::~Application() = default;

int Application::run() {
  LoggerService::initialize(std::make_unique<Logger>("engine", "logs/engine.log"));

  JM_LOG_INFO("Journeyman Engine Starting up...");
  JM_LOG_DEBUG("Debug logging active!");

  std::filesystem::path rootPath = std::string(kManifestEntryKey);
  if (_argc > 1) {
    rootPath = _argv[1];
  }

  std::filesystem::path rootDir;
  std::filesystem::path manifestPath;

  // Running bundled archive: rootDir is the archive file itself; AssetManager
  // detects the .jm extension and mounts the archive backend. The manifest
  // entry inside the archive is keyed at the resolver root by kManifestEntryKey
  // (same string the CLI's jm pack/jm run reference).
  if (rootPath.extension() == ".jm") {
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

  _engine = std::make_unique<Engine>(rootDir, manifestPath);
  _engine->initialize();
  _engine->run();

  JM_LOG_INFO("Journeyman Engine Shut Down");
  return 0;
}
