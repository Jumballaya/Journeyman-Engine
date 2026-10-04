#include "SystemScheduler.hpp"

#include <queue>
#include <tuple>

#include "../../tasks/TaskId.hpp"
#include "../World.hpp"

void SystemScheduler::update(World& world, float dt) {
  for (SystemId sid : executionOrder()) {
    if (_systems[sid]->enabled) {
      _systems[sid]->update(world, dt);
    }
  }
}

void SystemScheduler::clear() {
  _systems.clear();
  _access.clear();
  _tagProviders.clear();
  _systemTypes.clear();
  _systemJobMap.clear();
  _dependencyResolvers.clear();
  _order.clear();
  _orderDirty = true;
}

void SystemScheduler::disableSystem(System& system) {
  for (auto& s : _systems) {
    if (s.get() == &system) {
      s->enabled = false;
      break;
    }
  }
}

void SystemScheduler::enableSystem(System& system) {
  for (auto& s : _systems) {
    if (s.get() == &system) {
      s->enabled = true;
      break;
    }
  }
}

std::vector<SystemId> SystemScheduler::providersOf(SystemId sid) {
  std::vector<SystemId> providers;
  auto typeIt = _systemTypes.find(sid);
  if (typeIt == _systemTypes.end()) return providers;
  auto resolverIt = _dependencyResolvers.find(typeIt->second);
  if (resolverIt != _dependencyResolvers.end()) {
    resolverIt->second(*this, sid, providers);
  }
  std::erase(providers, sid);
  return providers;
}

bool SystemScheduler::conflicts(const Access& a, const Access& b) {
  if (a.exclusive || b.exclusive) return true;
  auto touches = [](const Access& x, std::type_index t) {
    return std::find(x.reads.begin(), x.reads.end(), t) != x.reads.end() ||
           std::find(x.writes.begin(), x.writes.end(), t) != x.writes.end();
  };
  for (const auto& w : a.writes) {
    if (touches(b, w)) return true;
  }
  for (const auto& w : b.writes) {
    if (touches(a, w)) return true;
  }
  return false;
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

  using Key = std::tuple<int, SystemId>;
  std::priority_queue<Key, std::vector<Key>, std::greater<Key>> ready;
  for (SystemId sid = 0; sid < n; ++sid) {
    if (inDegree[sid] == 0) ready.emplace(static_cast<int>(_access[sid].stage), sid);
  }

  _order.clear();
  while (!ready.empty()) {
    SystemId sid = std::get<1>(ready.top());
    ready.pop();
    _order.push_back(sid);
    for (SystemId dep : dependents[sid]) {
      if (--inDegree[dep] == 0) ready.emplace(static_cast<int>(_access[dep].stage), dep);
    }
  }

  // A DependsOn cycle leaves systems unscheduled; append them in registration
  // order rather than silently dropping them.
  if (_order.size() != n) {
    for (SystemId sid = 0; sid < n; ++sid) {
      if (std::find(_order.begin(), _order.end(), sid) == _order.end()) _order.push_back(sid);
    }
  }

  _orderDirty = false;
  return _order;
}

void SystemScheduler::buildTaskGraph(TaskGraph& graph, World& world, float dt) {
  _systemJobMap.clear();

  const auto& order = executionOrder();
  std::vector<SystemId> scheduled;
  scheduled.reserve(order.size());

  for (SystemId sid : order) {
    if (!_systems[sid]->enabled) continue;

    TaskId tid = graph.addTask([this, sid, &world, dt]() {
      _systems[sid]->update(world, dt);
    });
    _systemJobMap[sid] = tid;

    // Every edge points from an earlier system in `order` to a later one, so
    // the graph is acyclic by construction.
    for (SystemId earlier : scheduled) {
      if (conflicts(_access[earlier], _access[sid])) {
        graph.addDependency(tid, _systemJobMap[earlier]);
      }
    }
    for (SystemId provider : providersOf(sid)) {
      auto it = _systemJobMap.find(provider);
      if (it != _systemJobMap.end()) graph.addDependency(tid, it->second);
    }
    scheduled.push_back(sid);
  }
}
