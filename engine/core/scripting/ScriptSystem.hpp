#pragma once

#include "../app/GameClock.hpp"
#include "../ecs/World.hpp"
#include "../ecs/system/System.hpp"
#include "ScriptComponent.hpp"
#include "ScriptManager.hpp"

// Starts new scripts, delivers messages and last frame's physics events, then onUpdate
// (scaled dt; runWhenPaused scripts get unscaled dt while paused). Exclusive:
// scripts touch anything. A paused script's messages wait until it runs again.
class ScriptSystem : public System {
 public:
  ScriptSystem(ScriptManager& manager, const GameClock& clock) : _manager(manager), _clock(clock) {}

  void update(World& world, float dt) override {
    for (auto [entity, script] : world.view<ScriptComponent>()) {
      const LoadedScript* loaded = _manager.getScript(script->script);
      if (script->started) {
        // Multiplayer: the entity moved elsewhere (its simulation is another process's now); it stops.
        const bool movedAway = script->instance.isValid() && !_manager.runsHere(entity);
        // Hot reload: a new version restarts at once, fresh module globals, components and state kept.
        const bool reloaded = loaded && loaded->version != script->version && !world.isPendingDestroy(entity);
        if (!movedAway && !reloaded) continue;
        if (reloaded) _manager.restarting(entity);
        _manager.destroyInstance(script->instance);
        script->instance = {};
        script->started = false;
        script->params = script->startParams;
        if (movedAway) continue;
      }
      if (!script->script.isValid() || !_manager.runsHere(entity)) continue;
      script->started = true;
      script->version = loaded ? loaded->version : 0;
      script->startParams = script->params;
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
    for (const ScriptManager::Event& event : _manager.takeEvents()) notify(world, event);

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

  // Skips entities destroyed (or doomed) since it happened; an overlap's end
  // still reaches the one left when the other went.
  void notify(World& world, const ScriptManager::Event& e) {
    if (world.isPendingDestroy(e.self)) return;
    const bool otherGone = e.other != kNoEntityId && (world.isPendingDestroy(e.other) || !world.isAlive(e.other));
    if (otherGone && e.event != ScriptEvent::OverlapEnd) return;
    if (auto* script = world.getComponent<ScriptComponent>(e.self)) {
      if (ScriptInstance* instance = _manager.getInstance(script->instance)) instance->onEvent(e.event, e.other);
    }
  }
};
