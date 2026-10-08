#pragma once

#include <functional>
#include <memory>
#include <typeindex>
#include <utility>
#include <vector>

#include "../ecs/system/TypeList.hpp"
#include "EngineModule.hpp"
#include "ModuleTraits.hpp"

class Engine;

// An engine's modules, initialized in ModuleTraits dependency order
// and shut down in reverse.
class ModuleRegistry {
 public:
  // Constructs T in place, so its ModuleTraits are known.
  template <typename T, typename... Args>
  T& registerModule(Args&&... args);

  // A pre-built module: no traits, so it provides and depends on nothing.
  void registerModule(std::unique_ptr<EngineModule> module);

  void initializeModules(Engine& engine);
  void tickMainThreadModules(Engine& engine, float dt);
  void shutdownModules(Engine& engine);

  // The registered module of type T, or nullptr. Depend on its tag (ModuleTraits)
  // so it initializes first.
  template <typename T>
  T* find() const {
    for (const auto& entry : _modules) {
      if (auto* t = dynamic_cast<T*>(entry.module.get())) return t;
    }
    return nullptr;
  }

 private:
  struct Entry {
    std::unique_ptr<EngineModule> module;
    std::vector<std::type_index> provides, dependsOn;
  };

  template <typename List>
  static std::vector<std::type_index> tagsOf() {
    std::vector<std::type_index> tags;
    TypeListForEach<List>::apply([&]<typename Tag>() { tags.emplace_back(typeid(Tag)); });
    return tags;
  }

  std::vector<Entry> _modules;
  // Dependency order: ticks walk it forward, shutdown in reverse.
  std::vector<size_t> _initOrder;
};

// Every REGISTER_MODULE'd module, as a function that adds a fresh instance to a
// registry. Each Engine builds its own registry from this list.
std::vector<std::function<void(ModuleRegistry&)>>& ModuleCatalog();

template <typename T, typename... Args>
T& ModuleRegistry::registerModule(Args&&... args) {
  auto module = std::make_unique<T>(std::forward<Args>(args)...);
  T& ref = *module;
  _modules.push_back({std::move(module), tagsOf<typename ModuleTraits<T>::Provides>(),
                      tagsOf<typename ModuleTraits<T>::DependsOn>()});
  return ref;
}
