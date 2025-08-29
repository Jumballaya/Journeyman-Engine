#pragma once

#include <glm/glm.hpp>

#include "../core/ecs/component/Component.hpp"

struct VelocityComponent : public Component<VelocityComponent> {
  COMPONENT_NAME("VelocityComponent");
  glm::vec2 velocity{0.0f};
};

struct PODVelocityComponent {
  float vx, vy;
};

static_assert(std::is_trivially_copyable_v<PODVelocityComponent>, "POD must be trivially copyable");