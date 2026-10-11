#include "SoftFollow.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <functional>
#include <queue>
#include <utility>
#include <vector>

#include "Systems.hpp"
#include "TransformHierarchy.hpp"

namespace {

// Followers in the order they move: each after its parent (always) and after what it follows (unless
// that's a cycle: then the first one stuck goes anyway). Parents and targets are ordered too.
std::vector<EntityId> followOrder(World& world) {
  struct Node {
    EntityId id;
    int hard = 0, soft = 0;  // not yet placed: its parent (0 or 1), what it follows (0 or 1)
    std::vector<size_t> after;  // the nodes waiting for this one
    bool follows = false, done = false;
  };
  std::vector<Node> nodes;
  std::unordered_map<EntityId, size_t> index;
  const std::function<size_t(EntityId)> add = [&](EntityId id) -> size_t {
    if (const auto known = index.find(id); known != index.end()) return known->second;
    const size_t at = index[id] = nodes.size();
    nodes.push_back({id});
    if (const EntityId parent = world.parentOf(id); parent != kNoEntityId) {
      const size_t from = add(parent);
      nodes[from].after.push_back(at);
      ++nodes[at].hard;
    }
    if (auto* follow = world.getComponent<SoftFollowComponent>(id); follow && world.getComponent<TransformComponent>(id)) {
      nodes[at].follows = true;
      const EntityId target = follow->target.resolve(world);
      if (target != kNoEntityId && target != id && world.getComponent<TransformComponent>(target)) {
        const size_t from = add(target);
        nodes[from].after.push_back(at);
        ++nodes[at].soft;
      }
    }
    return at;
  };
  for (auto [entity, follow, transform] : world.view<SoftFollowComponent, TransformComponent>()) add(entity);
  // Ready ones in the order found (smallest first); a cycle stalls it, then the first `placeable`
  // (parent placed, maybe still waiting on what it follows) goes anyway.
  using Heap = std::priority_queue<size_t, std::vector<size_t>, std::greater<>>;
  Heap ready, placeable;
  for (size_t i = 0; i < nodes.size(); ++i) {
    if (nodes[i].hard == 0) placeable.push(i);
    if (nodes[i].hard == 0 && nodes[i].soft == 0) ready.push(i);
  }
  std::vector<EntityId> order;
  for (size_t placed = 0; placed < nodes.size(); ++placed) {
    Heap& from = ready.empty() ? placeable : ready;
    while (nodes[from.top()].done) from.pop();
    const size_t next = from.top();
    from.pop();
    Node& n = nodes[next];
    n.done = true;
    if (n.follows) order.push_back(n.id);
    for (size_t waiting : n.after) {
      Node& w = nodes[waiting];
      const bool parent = world.parentOf(w.id) == n.id && w.hard > 0;
      int& count = parent ? w.hard : w.soft;
      if (count == 0 || --count > 0 || w.done) continue;
      if (parent) placeable.push(waiting);
      if (w.hard == 0 && w.soft == 0) ready.push(waiting);
    }
  }
  return order;
}

}  // namespace

void SoftFollowSystem::update(World& world, float dt) {
  dt = simulationStep(dt);
  for (const EntityId entity : followOrder(world)) {
    auto* follow = world.getComponent<SoftFollowComponent>(entity);
    auto* transform = world.getComponent<TransformComponent>(entity);
    const EntityId target = follow->target.resolve(world);
    if (target == entity || !world.getComponent<TransformComponent>(target)) continue;
    const TransformComponent to = placedNow(world, target);  // a child's place this frame, not last
    TransformComponent self = placedNow(world, entity);
    const glm::vec2 offset = follow->mirror && to.scale.x < 0.0f ? glm::vec2(-follow->offset.x, follow->offset.y) : follow->offset;
    const glm::vec2 goal = glm::vec2(to.position) + offset;
    const float k = follow->smoothing > 0.0f && follow->started ? 1.0f - std::exp(-follow->smoothing * dt) : 1.0f;
    const glm::vec2 at = glm::vec2(self.position) + (goal - glm::vec2(self.position)) * k;
    self.position.x = at.x;
    self.position.y = at.y;
    transform->position = self.position;
    // A child is placed from its local transform: it follows there, under its parent as it is now.
    const EntityId parent = world.parentOf(entity);
    auto* local = world.getComponent<LocalTransformComponent>(entity);
    if (local && world.getComponent<TransformComponent>(parent)) {
      const glm::vec3 there = localTo(placedNow(world, parent), self).position;
      local->position.x = there.x;
      local->position.y = there.y;
    }
    follow->started = true;
  }
}
