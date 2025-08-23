#include "ModuleRegistry.hpp"

#include "../logger/logging.hpp"
#include "Application.hpp"
#include "EngineModule.hpp"

void ModuleRegistry::registerModule(std::unique_ptr<EngineModule> module) {
  modules.push_back(std::move(module));
}

void ModuleRegistry::initializeModules(Application& app) {
  JM_LOG_INFO("[ModuleRegistry] found: {} modules", modules.size());
  for (auto& module : modules) {
    module->initialize(app);
  }
}

void ModuleRegistry::tickMainThreadModules(Application& app, float dt) {
  for (auto& module : modules) {
    module->tickMainThread(app, dt);
  }
}

void ModuleRegistry::buildAsyncTicks(TaskGraph& graph, float dt) {
  for (auto& module : modules) {
    graph.addTask([module = module.get(), dt]() {
      module->tickAsync(dt);
    });
  }
}

void ModuleRegistry::shutdownModules(Application& app) {
  std::cout << "[APP Shutdown] 1" << std::endl;
  for (auto& module : modules) {
    module->shutdown(app);
  }
  std::cout << "[APP Shutdown] 2" << std::endl;

  modules.clear();

  std::cout << "[APP Shutdown] 3" << std::endl;
}

ModuleRegistry& GetModuleRegistry() {
  static ModuleRegistry instance;
  return instance;
}