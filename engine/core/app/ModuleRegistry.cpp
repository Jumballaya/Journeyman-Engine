#include "ModuleRegistry.hpp"

#include <queue>
#include <stdexcept>
#include <unordered_map>

#include "../logger/logging.hpp"
#include "Engine.hpp"
#include "EngineModule.hpp"

void ModuleRegistry::registerModule(std::unique_ptr<EngineModule> module) {
  _modules.push_back(std::move(module));
  _providesByModule.emplace_back();    // empty: no traits known
  _dependsOnByModule.emplace_back();
}

void ModuleRegistry::initializeModules(Engine& engine) {
  // Build tag -> providerIdx map. Duplicates: last wins with a warning
  // (mirrors SystemScheduler's tag-provider behavior).
  std::unordered_map<std::type_index, size_t> providerByTag;
  for (size_t i = 0; i < _modules.size(); ++i) {
    for (const auto& tag : _providesByModule[i]) {
      if (providerByTag.contains(tag)) {
        JM_LOG_WARN("[ModuleRegistry] duplicate provider for a tag; last wins (module '{}')",
                    _modules[i]->name());
      }
      providerByTag[tag] = i;
    }
  }

  // Build forward edges (provider -> dependents) and compute in-degrees.
  std::vector<std::vector<size_t>> dependents(_modules.size());
  std::vector<size_t> inDegree(_modules.size(), 0);
  for (size_t i = 0; i < _modules.size(); ++i) {
    for (const auto& tag : _dependsOnByModule[i]) {
      auto it = providerByTag.find(tag);
      if (it == providerByTag.end()) {
        JM_LOG_WARN("[ModuleRegistry] module '{}' depends on a tag with no provider; edge skipped",
                    _modules[i]->name());
        continue;
      }
      size_t providerIdx = it->second;
      dependents[providerIdx].push_back(i);
      ++inDegree[i];
    }
  }

  // Kahn's algorithm.
  std::queue<size_t> ready;
  for (size_t i = 0; i < _modules.size(); ++i) {
    if (inDegree[i] == 0) ready.push(i);
  }
  _initOrder.clear();
  _initOrder.reserve(_modules.size());
  while (!ready.empty()) {
    size_t idx = ready.front();
    ready.pop();
    _initOrder.push_back(idx);
    for (size_t dep : dependents[idx]) {
      if (--inDegree[dep] == 0) ready.push(dep);
    }
  }

  if (_initOrder.size() != _modules.size()) {
    throw std::runtime_error("[ModuleRegistry] cyclic module dependency detected");
  }

  JM_LOG_INFO("[ModuleRegistry] initializing {} modules (dep-sorted)", _modules.size());
  for (size_t idx : _initOrder) {
    _modules[idx]->initialize(engine);
  }
}

void ModuleRegistry::tickMainThreadModules(Engine& engine, float dt) {
  for (size_t idx : _initOrder) {
    _modules[idx]->tickMainThread(engine, dt);
  }
}

void ModuleRegistry::buildAsyncTicks(TaskGraph& graph, float dt) {
  for (size_t idx : _initOrder) {
    auto* mod = _modules[idx].get();
    graph.addTask([mod, dt]() { mod->tickAsync(dt); });
  }
}

void ModuleRegistry::shutdownModules(Engine& engine) {
  for (auto it = _initOrder.rbegin(); it != _initOrder.rend(); ++it) {
    auto& mod = _modules[*it];
    if (!mod) continue;
    JM_LOG_INFO("[ModuleRegistry] shutting down module '{}' (index {})", mod->name(), *it);
    mod->shutdown(engine);
  }
  _modules.clear();
  _providesByModule.clear();
  _dependsOnByModule.clear();
  _initOrder.clear();
  JM_LOG_INFO("[ModuleRegistry] all modules shutdown");
}

ModuleRegistry& GetModuleRegistry() {
  static ModuleRegistry instance;
  return instance;
}
