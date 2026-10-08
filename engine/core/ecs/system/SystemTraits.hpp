#pragma once

#include "TypeList.hpp"

// Frame phases: systems run in stage order, then DependsOn, then registration
// order, one at a time on the main thread.
enum class SystemStage : int {
  Input = 0,
  Logic = 100,        // scripts, gameplay
  Physics = 200,      // integrate motion
  PostPhysics = 300,  // collision response, animation
  Render = 400,       // draw-call collection
};

// Specialize per system (examples: physics2d/Physics2DModule.cpp). The scheduler
// orders by `stage` and by DependsOn/Provides tags; Reads/Writes document the
// components a system touches. An unspecialized system runs in the Logic stage.
template <typename T>
struct SystemTraits {
  using DependsOn = EmptyList;
  using Provides = EmptyList;
  using Reads = EmptyList;
  using Writes = EmptyList;
};
