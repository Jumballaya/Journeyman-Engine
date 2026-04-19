#pragma once

#include <memory>
#include <typeindex>
#include <utility>
#include <vector>

#include "../ecs/system/TypeList.hpp"
#include "../tasks/TaskGraph.hpp"
#include "EngineModule.hpp"
#include "ModuleTraits.hpp"

class Engine;

// ModuleRegistry holds the set of EngineModules registered (typically at
// static-init time via REGISTER_MODULE). On initializeModules() it
// topologically sorts them by their ModuleTraits — Provides/DependsOn tag
// typelists — and invokes their lifecycle methods in that order. Shutdown
// runs in reverse sorted order.
class ModuleRegistry {
 public:
  // Primary registration: constructs T in-place so the registry can capture
  // its ModuleTraits by template parameter.
  template <typename T, typename... Args>
  T& registerModule(Args&&... args);

  // Escape hatch: register a pre-constructed module whose traits are unknown
  // to the registry. Treated as having empty Provides/DependsOn.
  void registerModule(std::unique_ptr<EngineModule> module);

  void initializeModules(Engine& engine);
  void tickMainThreadModules(Engine& engine, float dt);
  void buildAsyncTicks(TaskGraph& graph, float dt);
  void shutdownModules(Engine& engine);

 private:
  std::vector<std::unique_ptr<EngineModule>> _modules;
  std::vector<std::vector<std::type_index>> _providesByModule;
  std::vector<std::vector<std::type_index>> _dependsOnByModule;

  // Topologically-sorted indices into _modules, computed in
  // initializeModules. tickMainThread/buildAsyncTicks walk this forward;
  // shutdown walks it in reverse.
  std::vector<size_t> _initOrder;
};

ModuleRegistry& GetModuleRegistry();

template <typename T, typename... Args>
T& ModuleRegistry::registerModule(Args&&... args) {
  auto module = std::make_unique<T>(std::forward<Args>(args)...);
  T& ref = *module;
  _modules.push_back(std::move(module));

  std::vector<std::type_index> provides;
  TypeListForEach<typename ModuleTraits<T>::Provides>::apply(
      [&]<typename Tag>() {
        provides.push_back(std::type_index(typeid(Tag)));
      });
  _providesByModule.push_back(std::move(provides));

  std::vector<std::type_index> dependsOn;
  TypeListForEach<typename ModuleTraits<T>::DependsOn>::apply(
      [&]<typename Tag>() {
        dependsOn.push_back(std::type_index(typeid(Tag)));
      });
  _dependsOnByModule.push_back(std::move(dependsOn));

  return ref;
}
