#include <iostream>
#include <thread>

#include "audio/AudioManager.hpp"
#include "audio/SoundBuffer.hpp"
#include "core/app/Application.hpp"
#include "core/assets/AssetManager.hpp"
#include "core/assets/FileSystem.hpp"
#include "core/assets/RawAsset.hpp"
#include "core/logger/logging.hpp"

int main(int argc, char** argv) {
  LoggerService::initialize(std::make_unique<Logger>("engine", "logs/engine.log"));

  JM_LOG_INFO("Journeyman Engine Starting up...");
  JM_LOG_DEBUG("Debug logging active!");

  std::filesystem::path rootPath = ".jm.json";
  if (argc > 1) {
    rootPath = argv[1];
  }

  std::filesystem::path rootDir;
  std::filesystem::path manifestPath;

  // Running bundled archive
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
  // Running bundled game from game folder
  // this looks for a ".jm.json" file in that folder
  else if (std::filesystem::is_directory(rootPath)) {
    rootDir = rootPath;
    manifestPath = ".jm.json";
    JM_LOG_INFO("[JSON] Mounting: {}", rootDir.string());
    JM_LOG_INFO("[JSON] Manifest: {}", manifestPath.string());
  } else {
    JM_LOG_ERROR("Unknown input type. Must be a .json, directory, or .jm archive.");
    return 1;
  }

  JM_LOG_INFO("Journeyman Engine Starting up...");
  Application app(rootDir, manifestPath);
  app.initialize();
  app.run();

  JM_LOG_INFO("Journeyman Engine Shut Down");
}