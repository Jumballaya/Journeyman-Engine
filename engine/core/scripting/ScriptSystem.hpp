#pragma once

#include "../ecs/system/System.hpp"

class ScriptSystem : public System {
 public:
  ScriptSystem(ScriptManager& manager) : _manager(manager) {}

  void update(World& world, float dt) {
    if (!enabled) {
      return;
    }
    for (auto [entity, script] : world.view<ScriptComponent>()) {
      _manager.updateInstance(script->instance, dt);
    }
  }

  const char* name() const override { return "ScriptSystem"; }

 private:
  ScriptManager& _manager;
};