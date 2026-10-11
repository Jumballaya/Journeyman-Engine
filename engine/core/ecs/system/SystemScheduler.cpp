#include "SystemScheduler.hpp"

#include <chrono>
#include <queue>
#include <tuple>

#include "../World.hpp"

std::vector<SystemId> SystemScheduler::providersOf(SystemId sid) const {
  std::vector<SystemId> providers;
  for (std::type_index tag : _systems[sid].dependsOn) {
    auto it = _tagProviders.find(tag);
    if (it != _tagProviders.end() && it->second != sid) providers.push_back(it->second);
  }
  return providers;
}

const std::vector<SystemId>& SystemScheduler::executionOrder() {
  if (!_orderDirty) return _order;

  // Kahn's algorithm over DependsOn edges; among ready systems pick the
  // lowest (stage, registration index). DependsOn always wins over stage.
  const size_t n = _systems.size();
  std::vector<std::vector<SystemId>> dependents(n);
  std::vector<size_t> inDegree(n, 0);
  for (SystemId sid = 0; sid < n; ++sid) {
    for (SystemId provider : providersOf(sid)) {
      dependents[provider].push_back(sid);
      ++inDegree[sid];
    }
  }

  using Key = std::tuple<SystemStage, SystemId>;
  std::priority_queue<Key, std::vector<Key>, std::greater<Key>> ready;
  for (SystemId sid = 0; sid < n; ++sid) {
    if (inDegree[sid] == 0) ready.emplace(_systems[sid].stage, sid);
  }

  _order.clear();
  while (!ready.empty()) {
    SystemId sid = std::get<1>(ready.top());
    ready.pop();
    _order.push_back(sid);
    for (SystemId dep : dependents[sid]) {
      if (--inDegree[dep] == 0) ready.emplace(_systems[dep].stage, dep);
    }
  }

  // A DependsOn cycle leaves systems unscheduled; append them in registration
  // order rather than silently dropping them.
  for (SystemId sid = 0; sid < n && _order.size() != n; ++sid) {
    if (inDegree[sid] != 0) _order.push_back(sid);
  }

  _orderDirty = false;
  return _order;
}

void SystemScheduler::run(World& world, float dt, SystemStage from, const Timed& timed) {
  for (SystemId sid : executionOrder()) {
    if (_systems[sid].stage < from) continue;
    System& system = *_systems[sid].system;
    if (!timed) {
      system.update(world, dt);
      continue;
    }
    const auto start = std::chrono::steady_clock::now();
    system.update(world, dt);
    timed(system.name(), std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
  }
}
