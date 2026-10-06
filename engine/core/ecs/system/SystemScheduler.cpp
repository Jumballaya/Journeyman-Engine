#include "SystemScheduler.hpp"

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

bool SystemScheduler::conflicts(const Entry& a, const Entry& b) {
  if (a.exclusive || b.exclusive) return true;
  auto writesTouched = [](const Entry& writer, const Entry& other) {
    return std::any_of(writer.writes.begin(), writer.writes.end(), [&](std::type_index t) {
      return std::find(other.reads.begin(), other.reads.end(), t) != other.reads.end() ||
             std::find(other.writes.begin(), other.writes.end(), t) != other.writes.end();
    });
  };
  return writesTouched(a, b) || writesTouched(b, a);
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

void SystemScheduler::buildTaskGraph(TaskGraph& graph, World& world, float dt, SystemStage from) {
  std::unordered_map<SystemId, TaskId> tasks;
  for (SystemId sid : executionOrder()) {
    const Entry& entry = _systems[sid];
    if (entry.stage < from) continue;

    TaskId tid = graph.addTask([system = entry.system.get(), &world, dt]() { system->update(world, dt); });
    // Every edge points from an earlier system in the order to a later one, so
    // the graph is acyclic by construction.
    const std::vector<SystemId> providers = providersOf(sid);
    for (auto [earlier, earlierTask] : tasks) {
      if (conflicts(_systems[earlier], entry) || std::find(providers.begin(), providers.end(), earlier) != providers.end()) {
        graph.addDependency(tid, earlierTask);
      }
    }
    tasks.emplace(sid, tid);
  }
}
