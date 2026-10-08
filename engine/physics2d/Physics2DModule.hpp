#pragma once

#include "../core/app/EngineModule.hpp"

class Engine;

// Transforms, velocities, lifetimes, scroll wrapping and box-collider overlaps (onCollide).
class Physics2DModule : public EngineModule {
 public:
  void registerComponents(Engine& app) override;
  void initialize(Engine& app) override;
  void shutdown(Engine&) override {}

  const char* name() const override { return "Physics2DModule"; }
};
