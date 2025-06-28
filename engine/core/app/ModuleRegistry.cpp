#include "ModuleRegistry.hpp"

#include "Application.hpp"
#include "EngineModule.hpp"

void ModuleRegistry::registerModule(std::unique_ptr<EngineModule> module) {
  modules.push_back(std::move(module));
}

void ModuleRegistry::initializeModules(Application& app) {
  for (auto& module : modules) {
    module->initialize(app);
  }
}

void ModuleRegistry::tickMainThreadModules(float dt) {
  for (auto& module : modules) {
    module->tickMainThread(dt);
  }
}

void ModuleRegistry::buildAsyncTicks(TaskGraph& graph, float dt) {
  for (auto& module : modules) {
    graph.addTask([module = module.get(), dt]() {
      module->tickAsync(dt);
    });
  }
}

void ModuleRegistry::shutdownModules() {
  for (auto& module : modules) {
    module->shutdown();
  }
  modules.clear();
}

static ModuleRegistry s_ModuleRegistry;

ModuleRegistry& GetModuleRegistry() {
  return s_ModuleRegistry;
}