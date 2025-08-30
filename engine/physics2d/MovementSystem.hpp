#pragma once

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"

class MovementSystem : public System {
 public:
  MovementSystem() = default;

  void update(World& world, float dt) override {
    if (!std::isfinite(dt) || dt < 0.0f) dt = 0.0f;
    constexpr float kMaxDt = 1.0f / 20.0f;  // 50 ms
    if (dt > kMaxDt) dt = kMaxDt;

    for (auto [entity, trans, vel] : world.view<TransformComponent, VelocityComponent>()) {
      if (vel->velocity[0] == 0.0f && vel->velocity[1] == 0.0f) {
        continue;
      }

      trans->position[0] += vel->velocity[0] * dt;
      trans->position[1] += vel->velocity[1] * dt;
    }
  }
};