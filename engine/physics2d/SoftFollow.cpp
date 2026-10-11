#include "SoftFollow.hpp"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

#include "Systems.hpp"
#include "TransformHierarchy.hpp"

void SoftFollowSystem::update(World& world, float dt) {
  dt = simulationStep(dt);
  // Parents before their children: a child follows under its parent where that one went this frame.
  std::vector<std::pair<int, EntityId>> order;
  for (auto [entity, follow, transform] : world.view<SoftFollowComponent, TransformComponent>()) {
    int depth = 0;
    for (EntityId up = world.parentOf(entity); up != kNoEntityId; up = world.parentOf(up)) ++depth;
    order.emplace_back(depth, entity);
  }
  std::stable_sort(order.begin(), order.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& [_, entity] : order) {
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
