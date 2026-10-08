#pragma once

#include <nlohmann/json.hpp>

#include "../assets/AssetHandle.hpp"
#include "../ecs/component/Component.hpp"
#include "ScriptInstanceHandle.hpp"

// Runs a script for this entity. The script starts (its top-level code runs)
// on the entity's first update, once all of the entity's components exist.
struct ScriptComponent : Component<ScriptComponent> {
  COMPONENT_NAME("ScriptComponent");

  AssetHandle script;
  nlohmann::json params = nlohmann::json::object();
  // Keep updating, with unscaled dt, while the GameClock is paused (pause menus).
  bool runWhenPaused = false;

  nlohmann::json startParams;  // params as authored: a script stopped and restarted elsewhere gets them again
  bool started = false;
  ScriptInstanceHandle instance;  // invalid if the script failed to start
};
