#pragma once

#include "../app/GameClock.hpp"
#include "../ecs/World.hpp"
#include "../ecs/system/System.hpp"
#include "ScriptComponent.hpp"
#include "ScriptManager.hpp"

// Starts new scripts, delivers messages and last frame's collisions, then onUpdate
// (scaled dt; runWhenPaused scripts get unscaled dt while paused). Exclusive:
// scripts touch anything. A paused script's messages wait until it runs again.
class ScriptSystem : public System {
 public:
  ScriptSystem(ScriptManager& manager, const GameClock& clock) : _manager(manager), _clock(clock) {}

  void update(World& world, float dt) override {
    for (auto [entity, script] : world.view<ScriptComponent>()) {
      if (script->started) continue;
      script->started = true;
      script->instance = _manager.createInstance(script->script, entity, std::move(script->params));
    }

    const bool paused = _clock.paused();
    for (auto& [to, message] : _manager.takeMessages()) {
      auto* script = world.isPendingDestroy(to) ? nullptr : world.getComponent<ScriptComponent>(to);
      if (!script) continue;  // no script to receive it
      if (paused && !script->runWhenPaused) {
        _manager.queueMessage(to, std::move(message));
        continue;
      }
      if (ScriptInstance* instance = _manager.getInstance(script->instance)) instance->onMessage(message);
    }
    for (auto [a, b] : _manager.takeCollisions()) {
      notify(world, a, b);
      notify(world, b, a);
    }

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
