#pragma once

#include "TypeList.hpp"

// Frame phases: conflicting systems run in stage order (then registration
// order); non-conflicting ones still run in parallel.
enum class SystemStage : int {
  Input = 0,
  Logic = 100,        // scripts, gameplay
  Physics = 200,      // integrate motion
  PostPhysics = 300,  // collision response, animation
  Render = 400,       // draw-call collection
};

// Reads/Writes wildcard: touches anything, so the system runs alone.
struct AnyComponent {};

// Specialize per system (examples: physics2d/Traits.hpp). Systems conflict when
// one writes what the other touches; unspecialized systems are assumed to touch anything.
template <typename T>
struct SystemTraits {
  using DependsOn = EmptyList;
  using Provides = EmptyList;
  using Reads = EmptyList;
  using Writes = EmptyList;
  static constexpr bool kUndeclared = true;
};
