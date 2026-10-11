#pragma once

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"
#include "../core/ecs/system/SystemTraits.hpp"
#include "SoftFollowComponent.hpp"
#include "Systems.hpp"
#include "TransformComponent.hpp"
#include "TransformHierarchy.hpp"

// Eases each soft follower toward its target's position plus its offset, after
// things moved; one without a target stays where it is.
class SoftFollowSystem : public System {
 public:
  void update(World& world, float dt) override;
  const char* name() const override { return "SoftFollowSystem"; }
};

// After movement, before the hierarchy places children (a follower's ride along this frame).
template <>
struct SystemTraits<SoftFollowSystem> {
  using DependsOn = TypeList<Physics2D_Moved>;
  using Provides = EmptyList;
  using Reads = EmptyList;
  using Writes = TypeList<SoftFollowComponent, TransformComponent, LocalTransformComponent>;
  static constexpr SystemStage stage = SystemStage::Physics;
};
