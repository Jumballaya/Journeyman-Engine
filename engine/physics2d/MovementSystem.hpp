#pragma once

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"

class MovementSystem : public System {
 public:
  MovementSystem() = default;

  void update(World& world, float dt) override {
    for (auto [entity, trans, vel] : world.view<TransformComponent, VelocityComponent>()) {
      trans->position[0] += vel->velocity[0];
      trans->position[1] += vel->velocity[1];
    }
  }
};