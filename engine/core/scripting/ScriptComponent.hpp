#pragma once

#include "../ecs/component/Component.hpp"
#include "ScriptInstanceHandle.hpp"

struct ScriptComponent : Component<ScriptComponent> {
  COMPONENT_NAME("ScriptComponent");
  ScriptComponent() = default;
  explicit ScriptComponent(ScriptInstanceHandle handle) : instance(handle) {}

  ScriptInstanceHandle instance;
};