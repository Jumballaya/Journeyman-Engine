#pragma once

#include <cmath>

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"
#include "LifetimeComponent.hpp"
#include "ScrollWrapComponent.hpp"
#include "TransformComponent.hpp"

class LifetimeSystem : public System {
 public:
  void update(World& world, float dt) override {
    for (auto [entity, life] : world.view<LifetimeComponent>()) {
      life->seconds -= dt;
      if (life->seconds <= 0.0f) world.destroyDeferred(entity);
    }
  }
  const char* name() const override { return "LifetimeSystem"; }
};

class ScrollWrapSystem : public System {
 public:
  void update(World& world, float) override {
    for (auto [entity, wrap, trans] : world.view<ScrollWrapComponent, TransformComponent>()) {
      const float span = wrap->maxY - wrap->minY;
      if (span <= 0.0f) continue;
      float& y = trans->position.y;
      if (y < wrap->minY) y += span * std::ceil((wrap->minY - y) / span);
      if (y > wrap->maxY) y -= span * std::ceil((y - wrap->maxY) / span);
    }
  }
  const char* name() const override { return "ScrollWrapSystem"; }
};
