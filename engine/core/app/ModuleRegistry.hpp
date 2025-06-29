#pragma once

#include <memory>
#include <vector>

#include "../tasks/TaskGraph.hpp"
#include "EngineModule.hpp"

class Application;

class ModuleRegistry {
 public:
  void registerModule(std::unique_ptr<EngineModule> module);

  void initializeModules(Application& app);
  void tickMainThreadModules(float dt);
  void buildAsyncTicks(TaskGraph& graph, float dt);

  void shutdownModules(Application& app);

 private:
  std::vector<std::unique_ptr<EngineModule>> modules;
};

ModuleRegistry& GetModuleRegistry();