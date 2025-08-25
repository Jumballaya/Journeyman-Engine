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
  for (std::size_t i = modules.size(); i-- > 0;) {
    auto& mod = modules[i];
    if (!mod) continue;
    JM_LOG_ERROR("[ModuleRegistry] shutting down module '{}' (index {})", mod->name(), i);
    mod->shutdown(app);
  }
  modules.clear();
  JM_LOG_ERROR("[ModuleRegistry] all modules shutdown");
}

ModuleRegistry& GetModuleRegistry() {
  static ModuleRegistry instance;
  return instance;
}