#pragma once

#include "../app/GameClock.hpp"
#include "../ecs/system/System.hpp"
#include "ScriptComponent.hpp"
#include "ScriptManager.hpp"

// Runs every script's onUpdate. Gameplay scripts get scaled dt and are
// skipped entirely while the clock is paused; runWhenPaused scripts always
// run with unscaled dt. No SystemTraits: scripts may touch anything, so the
// scheduler treats this system as exclusive.
class ScriptSystem : public System {
 public:
  ScriptSystem(ScriptManager& manager, const GameClock& clock) : _manager(manager), _clock(clock) {}

  void update(World& world, float dt) override {
    if (!enabled) {
      return;
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
};
