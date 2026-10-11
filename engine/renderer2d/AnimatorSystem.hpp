#pragma once

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"
#include "AnimatorComponent.hpp"

// Plays each animator's first state whose condition holds (a held state
// first finishes its clip) on the entity's SpriteAnimationComponent.
class AnimatorSystem : public System {
 public:
  void update(World& world, float dt) override;
  const char* name() const override { return "AnimatorSystem"; }
};
