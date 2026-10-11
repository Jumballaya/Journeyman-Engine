#include "SoftFollow.hpp"

#include <cmath>

#include "Systems.hpp"

void SoftFollowSystem::update(World& world, float dt) {
  dt = simulationStep(dt);
  for (auto [entity, follow, transform] : world.view<SoftFollowComponent, TransformComponent>()) {
    const auto* to = world.getComponent<TransformComponent>(follow->target.resolve(world));
    if (!to || to == transform) continue;
    const glm::vec2 offset = follow->mirror && to->scale.x < 0.0f ? glm::vec2(-follow->offset.x, follow->offset.y) : follow->offset;
    const glm::vec2 goal = glm::vec2(to->position) + offset;
    const float k = follow->smoothing > 0.0f && follow->started ? 1.0f - std::exp(-follow->smoothing * dt) : 1.0f;
    const glm::vec2 at = glm::vec2(transform->position) + (goal - glm::vec2(transform->position)) * k;
    transform->position.x = at.x;
    transform->position.y = at.y;
    follow->started = true;
  }
}
