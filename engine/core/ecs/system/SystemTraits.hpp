#pragma once

#include "TypeList.hpp"

// Coarse frame phases. Systems run in stage order unless an explicit
// DependsOn edge says otherwise; within a stage, registration order breaks
// ties. Stages only order systems whose data accesses conflict (see below) —
// non-conflicting systems still run in parallel.
enum class SystemStage : int {
  Input = 0,
  Logic = 100,        // scripts, gameplay
  Physics = 200,      // integrate motion
  PostPhysics = 300,  // collision response, animation
  Render = 400,       // draw-call collection
};

// Wildcard for Reads/Writes: "touches arbitrary component data". A system that
// lists it (e.g. anything that runs scripts) conflicts with every other system
// and therefore never runs concurrently with one.
struct AnyComponent {};

// Specialize per system:
//   template <> struct SystemTraits<MySystem> {
//     using DependsOn = TypeList<SomeTag>;     // run after the tag's provider
//     using Provides  = TypeList<OtherTag>;
//     using Reads     = TypeList<Transform>;
//     using Writes    = TypeList<Velocity>;
//     static constexpr SystemStage stage = SystemStage::Physics;  // optional
//   };
//
// Two systems conflict when one writes a component the other reads or writes.
// Conflicting systems are serialized; the scheduler never runs them at the
// same time. A system WITHOUT a specialization is assumed to touch anything
// (the safe default) — kUndeclared marks the primary template.
template <typename T>
struct SystemTraits {
  using DependsOn = EmptyList;
  using Provides = EmptyList;
  using Reads = EmptyList;
  using Writes = EmptyList;
  static constexpr bool kUndeclared = true;
};
