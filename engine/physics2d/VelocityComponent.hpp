#pragma once

#include <glm/glm.hpp>

#include "../core/ecs/component/Component.hpp"

// World units per second, changing by `acceleration` per second (gravity).
struct VelocityComponent : public Component<VelocityComponent> {
  COMPONENT_NAME("VelocityComponent");
  glm::vec2 velocity{0.0f};
  glm::vec2 acceleration{0.0f};
};


