#pragma once

#include <filesystem>

#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"
#include "../tasks/JobSystem.hpp"
#include "ModuleRegistry.hpp"

class Application {
 public:
  Application(const std::filesystem::path& rootDir, const std::filesystem::path& manifestPath);
  ~Application();

  void initialize();
  void run();
  void shutdown();

  World& getWorld();
  JobSystem& getJobSystem();

 private:
  bool running = false;

  std::filesystem::path _manifestPath;
  std::filesystem::path _rootDir;

  World ecsWorld;
  JobSystem jobSystem;
  ModuleRegistry moduleRegistry;
  AssetManager assetManager;
};