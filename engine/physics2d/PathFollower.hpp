#pragma once

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"
#include "../core/ecs/system/SystemTraits.hpp"
#include "PathFollowerComponent.hpp"
#include "TransformComponent.hpp"
#include "TransformHierarchy.hpp"
#include "VelocityComponent.hpp"

// Moves path followers on (waiting at points, turning or wrapping at the ends):
// a velocity to where they go this step (movement then carries riders), else
// their transform there.
class PathFollowerSystem : public System {
 public:
  void update(World& world, float dt) override;
  const char* name() const override { return "PathFollowerSystem"; }
};

template <>
struct SystemTraits<PathFollowerSystem> {
  using DependsOn = EmptyList;
  using Provides = EmptyList;
  using Reads = EmptyList;
  using Writes = TypeList<PathFollowerComponent, VelocityComponent, TransformComponent, LocalTransformComponent>;
  static constexpr SystemStage stage = SystemStage::Logic;  // before movement steps the velocity
};
