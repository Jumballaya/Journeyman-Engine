#pragma once

#include "../core/ecs/system/SystemTraits.hpp"
#include "../physics2d/TransformComponent.hpp"
#include "../physics2d/VelocityComponent.hpp"
#include "AnimatorSystem.hpp"
#include "Cameras.hpp"
#include "Particles.hpp"
#include "Renderer2DSystem.hpp"
#include "SpriteAnimationComponent.hpp"
#include "SpriteAnimationSystem.hpp"
#include "SpriteComponent.hpp"

// Renderer2D Tags
struct Renderer2D_AnimationsApplied {};  // produced by SpriteAnimationSystem
struct Renderer2D_ClipsChosen {};         // produced by AnimatorSystem

template <>
struct SystemTraits<AnimatorSystem> {
  using DependsOn = EmptyList;
  using Provides  = TypeList<Renderer2D_ClipsChosen>;
  using Reads     = TypeList<VelocityComponent>;
  using Writes    = TypeList<AnimatorComponent, SpriteAnimationComponent>;
  static constexpr SystemStage stage = SystemStage::PostPhysics;
};

template <>
struct SystemTraits<SpriteAnimationSystem> {
  using DependsOn = TypeList<Renderer2D_ClipsChosen>;
  using Provides  = TypeList<Renderer2D_AnimationsApplied>;
  using Reads     = TypeList<SpriteAnimationComponent>;
  using Writes    = TypeList<SpriteComponent, SpriteAnimationComponent>;
  static constexpr SystemStage stage = SystemStage::PostPhysics;
};

template <>
struct SystemTraits<Renderer2DSystem> {
  using DependsOn = TypeList<Renderer2D_AnimationsApplied>;
  using Provides  = EmptyList;
  using Reads     = TypeList<SpriteComponent, TransformComponent, ParticleEmitterComponent, GroundComponent>;
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

template <>
struct SystemTraits<CameraSystem> {
  using DependsOn = EmptyList;
  using Provides  = EmptyList;
  using Reads     = TypeList<VelocityComponent>;
  using Writes    = TypeList<CameraComponent, TransformComponent>;
  static constexpr SystemStage stage = SystemStage::PostPhysics;  // after things moved; still in a stopped editor
};
