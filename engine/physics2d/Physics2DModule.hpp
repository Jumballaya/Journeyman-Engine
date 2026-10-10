#pragma once

#include "../core/app/EngineModule.hpp"
#include "Blocking.hpp"

class Engine;

// Transforms, velocities, lifetimes, scroll wrapping and box-collider overlaps (onCollide).
class Physics2DModule : public EngineModule {
 public:
  void registerComponents(Engine& app) override;
  void bindScriptApi(Engine& app) override;
  void initialize(Engine& app) override;
  void shutdown(Engine&) override {}
  void tickMainThread(Engine&, float) override { _moves.clear(); }

  const char* name() const override { return "Physics2DModule"; }

 private:
  MoveFrame _moves;  // this frame's: scripts' moves, then velocities'
};
