#pragma once

#include "../core/ecs/system/SystemTraits.hpp"
#include "../physics2d/TransformComponent.hpp"
#include "Particles.hpp"
#include "Renderer2DSystem.hpp"
#include "SpriteAnimationComponent.hpp"
#include "SpriteAnimationSystem.hpp"
#include "SpriteComponent.hpp"

// Renderer2D Tags
struct Renderer2D_AnimationsApplied {};  // produced by SpriteAnimationSystem

template <>
struct SystemTraits<SpriteAnimationSystem> {
  using DependsOn = EmptyList;
  using Provides  = TypeList<Renderer2D_AnimationsApplied>;
  using Reads     = TypeList<SpriteAnimationComponent>;
  using Writes    = TypeList<SpriteComponent, SpriteAnimationComponent>;
  static constexpr SystemStage stage = SystemStage::PostPhysics;
};

template <>
struct SystemTraits<Renderer2DSystem> {
  using DependsOn = TypeList<Renderer2D_AnimationsApplied>;
  using Provides  = EmptyList;
  using Reads     = TypeList<SpriteComponent, TransformComponent, ParticleEmitterComponent, TerrainComponent>;
  using Writes    = EmptyList;
  static constexpr SystemStage stage = SystemStage::Render;
};

template <>
struct SystemTraits<ParticleSystem> {
  using DependsOn = EmptyList;
  using Provides  = EmptyList;
  using Reads     = TypeList<TransformComponent>;
  using Writes    = TypeList<ParticleEmitterComponent>;
  static constexpr SystemStage stage = SystemStage::PostPhysics;  // a simulation stage: still in a stopped editor
};
