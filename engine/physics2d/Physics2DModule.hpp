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
  void tickMainThread(Engine&, float) override { _carrying.carrier.clear(); }
  // The driver's `near tag=Name [distance]`: what's within distance of its colliders, nearest first.
  bool driveCommand(Engine& app, std::string_view verb, std::string_view args, nlohmann::json& reply) override;

  const char* name() const override { return "Physics2DModule"; }

 private:
  CarryFrame _carrying;  // scripts' moves this frame
};
