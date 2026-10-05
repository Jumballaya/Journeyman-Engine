#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "../../tasks/TaskGraph.hpp"
#include "System.hpp"
#include "SystemId.hpp"
#include "SystemTraits.hpp"

class World;

class SystemScheduler {
 public:
  SystemScheduler() = default;
  ~SystemScheduler() = default;
  SystemScheduler(const SystemScheduler&) = delete;
  SystemScheduler& operator=(const SystemScheduler&) = delete;
  SystemScheduler(SystemScheduler&&) noexcept = default;
  SystemScheduler& operator=(SystemScheduler&&) noexcept = default;

  template <typename T, typename... Args>
  void registerSystem(Args&&... args);

  void update(World& world, float dt);
  void clear();
  void disableSystem(System& system);
  void enableSystem(System& system);

  // One task per enabled system; DependsOn tags and data conflicts (SystemTraits.hpp)
  // become edges, so conflicting systems never run concurrently.
  // Systems in stages before `from` are left out.
  void buildTaskGraph(TaskGraph& graph, World& world, float dt, SystemStage from = SystemStage::Input);

  // Systems in the order conflicts are serialized: stage, then DependsOn
  // topology, then registration order. Exposed for tests and diagnostics.
  const std::vector<SystemId>& executionOrder();

 private:
  struct Access {
    std::vector<std::type_index> reads;
    std::vector<std::type_index> writes;
    bool exclusive = false;
    SystemStage stage = SystemStage::Logic;
  };

  std::vector<std::shared_ptr<System>> _systems;
  std::vector<Access> _access;  // parallel to _systems
  std::unordered_map<std::type_index, SystemId> _tagProviders;
  std::unordered_map<SystemId, std::type_index> _systemTypes;
  std::unordered_map<SystemId, TaskId> _systemJobMap;
  std::unordered_map<std::type_index, std::function<void(SystemScheduler&, SystemId, std::vector<SystemId>&)>> _dependencyResolvers;

  std::vector<SystemId> _order;
  bool _orderDirty = true;

  std::vector<SystemId> providersOf(SystemId sid);
  static bool conflicts(const Access& a, const Access& b);
};

template <typename T, typename... Args>
void SystemScheduler::registerSystem(Args&&... args) {
  static_assert(std::is_base_of_v<System, T>, "T must derive from System");
  using Traits = SystemTraits<T>;

  auto system = std::make_shared<T>(std::forward<Args>(args)...);
  SystemId id = static_cast<SystemId>(_systems.size());
  std::type_index systemType = std::type_index(typeid(T));
  _systemTypes.emplace(id, systemType);

  TypeListForEach<typename Traits::Provides>::apply(
      [&]<typename Tag>() { _tagProviders[std::type_index(typeid(Tag))] = id; });

  Access access;
  TypeListForEach<typename Traits::Reads>::apply(
      [&]<typename C>() { access.reads.push_back(std::type_index(typeid(C))); });
  TypeListForEach<typename Traits::Writes>::apply(
      [&]<typename C>() { access.writes.push_back(std::type_index(typeid(C))); });
  const auto any = std::type_index(typeid(AnyComponent));
  access.exclusive = requires { Traits::kUndeclared; } ||
                     std::find(access.reads.begin(), access.reads.end(), any) != access.reads.end() ||
                     std::find(access.writes.begin(), access.writes.end(), any) != access.writes.end();
  if constexpr (requires { Traits::stage; }) {
    access.stage = Traits::stage;
  }
  _access.push_back(std::move(access));

  // Resolves this type's DependsOn tags to provider SystemIds (once per type).
  if (_dependencyResolvers.find(systemType) == _dependencyResolvers.end()) {
    _dependencyResolvers[systemType] = [](SystemScheduler& scheduler, SystemId, std::vector<SystemId>& out) {
      TypeListForEach<typename Traits::DependsOn>::apply(
          [&]<typename Tag>() {
            auto it = scheduler._tagProviders.find(std::type_index(typeid(Tag)));
            if (it != scheduler._tagProviders.end()) out.push_back(it->second);
          });
    };
  }

  _systems.emplace_back(std::move(system));
  _orderDirty = true;
}
