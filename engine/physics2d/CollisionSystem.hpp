#pragma once

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"
#include "../core/events/EventBus.hpp"
#include "Physics2DEvents.hpp"
#include "VelocityComponent.hpp"
#include "BoxColliderComponent.hpp"
#include "TransformComponent.hpp"

class CollisionSystem : public System {
 public:
  CollisionSystem(EventBus& bus) : _eventBus(bus) {};

  void update(World& world, float dt) override {
    for (auto [entity, trans, collider, vel] : world.view<TransformComponent, BoxColliderComponent, VelocityComponent>()) {
      // If collide: create collision event
    }
  }

 private:
  EventBus& _eventBus;
};