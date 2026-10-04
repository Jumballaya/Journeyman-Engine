#pragma once

#include "../core/ecs/system/SystemTraits.hpp"
#include "BoxColliderComponent.hpp"
#include "CollisionSystem.hpp"
#include "LifetimeSystem.hpp"
#include "MovementSystem.hpp"
#include "TransformComponent.hpp"
#include "VelocityComponent.hpp"

class CollisionSystem;
class MovementSystem;

// Physics2D Tags
struct Physics2D_Moved {};              // produced by MovementSystem
struct Physics2D_CollisionsEmitted {};  // produced by CollisionSystem

// MovementSystem
template <>
struct SystemTraits<MovementSystem> {
  using DependsOn = EmptyList;
  using Provides = TypeList<Physics2D_Moved>;
  using Reads = TypeList<VelocityComponent, TransformComponent>;
  using Writes = TypeList<TransformComponent>;
  static constexpr SystemStage stage = SystemStage::Physics;
};

// CollisionSystem
template <>
struct SystemTraits<CollisionSystem> {
  using DependsOn = TypeList<Physics2D_Moved>;
  using Provides = TypeList<Physics2D_CollisionsEmitted>;
  using Reads = TypeList<TransformComponent, BoxColliderComponent, VelocityComponent>;
  using Writes = EmptyList;
  static constexpr SystemStage stage = SystemStage::PostPhysics;
};
template <>
struct SystemTraits<LifetimeSystem> {
  using DependsOn = EmptyList;
  using Provides = EmptyList;
  using Reads = EmptyList;
  using Writes = TypeList<LifetimeComponent>;
  static constexpr SystemStage stage = SystemStage::Physics;
};

template <>
struct SystemTraits<ScrollWrapSystem> {
  using DependsOn = TypeList<Physics2D_Moved>;
  using Provides = EmptyList;
  using Reads = TypeList<ScrollWrapComponent>;
  using Writes = TypeList<TransformComponent>;
  static constexpr SystemStage stage = SystemStage::Physics;
};
