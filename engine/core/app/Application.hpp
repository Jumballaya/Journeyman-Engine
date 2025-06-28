#pragma once

#include <filesystem>

#include "../ecs/World.hpp"
#include "../tasks/JobSystem.hpp"
#include "ModuleRegistry.hpp"

class Application {
 public:
  Application();
  ~Application();

  void initialize(const std::filesystem::path& gameManifestPath = ".jm.json");
  void run();
  void shutdown();

  World& getWorld();
  JobSystem& getJobSystem();

 private:
  bool running = false;

  World ecsWorld;
  JobSystem jobSystem;
  ModuleRegistry moduleRegistry;
};