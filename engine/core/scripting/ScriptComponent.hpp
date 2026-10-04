#pragma once

#include "../ecs/component/Component.hpp"
#include "ScriptInstanceHandle.hpp"

struct ScriptComponent : Component<ScriptComponent> {
  COMPONENT_NAME("ScriptComponent");
  ScriptComponent() = default;
  explicit ScriptComponent(ScriptInstanceHandle handle) : instance(handle) {}

  ScriptInstanceHandle instance;
  // Keep updating while the GameClock is paused (pause menus, overlays).
  // Such scripts always receive unscaled dt.
  bool runWhenPaused = false;
};

struct PODScriptComponent {};