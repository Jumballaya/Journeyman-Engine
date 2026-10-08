#include "ModuleRegistry.hpp"

#include <queue>
#include <stdexcept>
#include <unordered_map>

#include "../logger/logging.hpp"

void ModuleRegistry::registerModule(std::unique_ptr<EngineModule> module) {
  _modules.push_back({std::move(module), {}, {}});
}

void ModuleRegistry::sortByDependencies() {
  // Duplicate providers: last wins with a warning (as SystemScheduler does).
  std::unordered_map<std::type_index, size_t> providerByTag;
  for (size_t i = 0; i < _modules.size(); ++i) {
    for (const auto& tag : _modules[i].provides) {
      if (!providerByTag.insert_or_assign(tag, i).second) {
        JM_LOG_WARN("[ModuleRegistry] duplicate provider for a tag; last wins (module '{}')", _modules[i].module->name());
      }
    }
  }

  // Kahn's algorithm over provider -> dependent edges.
  std::vector<std::vector<size_t>> dependents(_modules.size());
  std::vector<size_t> inDegree(_modules.size(), 0);
  for (size_t i = 0; i < _modules.size(); ++i) {
    for (const auto& tag : _modules[i].dependsOn) {
      auto it = providerByTag.find(tag);
      if (it == providerByTag.end()) {
        JM_LOG_WARN("[ModuleRegistry] module '{}' depends on a tag with no provider; edge skipped",
                    _modules[i].module->name());
        continue;
      }
      dependents[it->second].push_back(i);
      ++inDegree[i];
    }
  }
  std::queue<size_t> ready;
  for (size_t i = 0; i < _modules.size(); ++i) {
    if (inDegree[i] == 0) ready.push(i);
  }
  _initOrder.clear();
  while (!ready.empty()) {
    const size_t idx = ready.front();
    ready.pop();
    _initOrder.push_back(idx);
    for (size_t dep : dependents[idx]) {
      if (--inDegree[dep] == 0) ready.push(dep);
    }
  }
  if (_initOrder.size() != _modules.size()) throw std::runtime_error("[ModuleRegistry] cyclic module dependency detected");
}

void ModuleRegistry::registerComponents(Engine& engine) {
  sortByDependencies();
  for (size_t idx : _initOrder) _modules[idx].module->registerComponents(engine);
}

void ModuleRegistry::bindScriptApis(Engine& engine) {
  if (_initOrder.size() != _modules.size()) sortByDependencies();
  for (size_t idx : _initOrder) _modules[idx].module->bindScriptApi(engine);
}

void ModuleRegistry::initializeModules(Engine& engine) {
  if (_initOrder.size() != _modules.size()) sortByDependencies();
  JM_LOG_INFO("[ModuleRegistry] initializing {} modules (dep-sorted)", _modules.size());
  for (size_t idx : _initOrder) _modules[idx].module->initialize(engine);
}

void ModuleRegistry::describeState(Engine& engine, nlohmann::json& state) {
  for (size_t idx : _initOrder) _modules[idx].module->describeState(engine, state);
}

void ModuleRegistry::tickMainThreadModules(Engine& engine, float dt) {
  for (size_t idx : _initOrder) _modules[idx].module->tickMainThread(engine, dt);
}

void ModuleRegistry::shutdownModules(Engine& engine) {
  for (auto it = _initOrder.rbegin(); it != _initOrder.rend(); ++it) {
    EngineModule& module = *_modules[*it].module;
    JM_LOG_INFO("[ModuleRegistry] shutting down module '{}' (index {})", module.name(), *it);
    module.shutdown(engine);
  }
  _modules.clear();
  _initOrder.clear();
  JM_LOG_INFO("[ModuleRegistry] all modules shutdown");
}

std::vector<std::function<void(ModuleRegistry&)>>& ModuleCatalog() {
  static std::vector<std::function<void(ModuleRegistry&)>> catalog;
  return catalog;
}
