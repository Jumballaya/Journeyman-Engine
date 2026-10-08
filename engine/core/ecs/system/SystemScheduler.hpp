#pragma once

#include <algorithm>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "System.hpp"
#include "SystemId.hpp"
#include "SystemTraits.hpp"

class World;

class SystemScheduler {
 public:
  template <typename T, typename... Args>
  void registerSystem(Args&&... args);

  // Updates every system, one at a time on the calling thread, in
  // executionOrder(); systems in stages before `from` are skipped. The same
  // order every frame is what makes a run repeatable.
  void run(World& world, float dt, SystemStage from = SystemStage::Input);

  // Stage, then DependsOn topology, then registration order. Exposed for tests
  // and diagnostics.
  const std::vector<SystemId>& executionOrder();

 private:
  struct Entry {
    std::unique_ptr<System> system;
    std::vector<std::type_index> dependsOn;
    SystemStage stage = SystemStage::Logic;
  };

  template <typename List>
  static std::vector<std::type_index> typeIndices() {
    std::vector<std::type_index> out;
    TypeListForEach<List>::apply([&]<typename C>() { out.emplace_back(typeid(C)); });
    return out;
  }
  std::vector<SystemId> providersOf(SystemId sid) const;

  std::vector<Entry> _systems;  // indexed by SystemId
  std::unordered_map<std::type_index, SystemId> _tagProviders;
  std::vector<SystemId> _order;
  bool _orderDirty = true;
};

template <typename T, typename... Args>
void SystemScheduler::registerSystem(Args&&... args) {
  static_assert(std::is_base_of_v<System, T>, "T must derive from System");
  using Traits = SystemTraits<T>;

  const auto id = static_cast<SystemId>(_systems.size());
  for (std::type_index tag : typeIndices<typename Traits::Provides>()) _tagProviders.insert_or_assign(tag, id);

  Entry entry{std::make_unique<T>(std::forward<Args>(args)...), typeIndices<typename Traits::DependsOn>()};
  if constexpr (requires { Traits::stage; }) entry.stage = Traits::stage;

  _systems.push_back(std::move(entry));
  _orderDirty = true;
}
