#pragma once

#include "../app/GameClock.hpp"
#include "../ecs/World.hpp"
#include "../ecs/system/System.hpp"
#include "ScriptComponent.hpp"
#include "ScriptManager.hpp"

// Starts new scripts, delivers last frame's collisions, then onUpdate (scaled dt;
// runWhenPaused scripts get unscaled dt while paused). Exclusive: scripts touch anything.
class ScriptSystem : public System {
 public:
  ScriptSystem(ScriptManager& manager, const GameClock& clock) : _manager(manager), _clock(clock) {}

  void update(World& world, float dt) override {
    for (auto [entity, script] : world.view<ScriptComponent>()) {
      if (script->started) continue;
      script->started = true;
      script->instance = _manager.createInstance(script->script, entity, std::move(script->params));
    }
    for (auto [a, b] : _manager.takeCollisions()) {
      notify(world, a, b);
      notify(world, b, a);
    }

    const bool paused = _clock.paused();
    for (auto [entity, script] : world.view<ScriptComponent>()) {
      if (paused && !script->runWhenPaused) continue;
      if (ScriptInstance* instance = _manager.getInstance(script->instance)) {
        instance->update(script->runWhenPaused ? _clock.unscaledDt() : dt);
      }
    }
  }

  const char* name() const override { return "ScriptSystem"; }

 private:
  ScriptManager& _manager;
  const GameClock& _clock;

  // Skips entities destroyed (or doomed) since the contact was detected.
  void notify(World& world, EntityId self, EntityId other) {
    if (world.isPendingDestroy(self) || world.isPendingDestroy(other) || !world.isAlive(other)) return;
    if (auto* script = world.getComponent<ScriptComponent>(self)) {
      if (ScriptInstance* instance = _manager.getInstance(script->instance)) instance->onCollide(other);
    }
  }
};
