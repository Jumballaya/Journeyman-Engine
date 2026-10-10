#pragma once

#include <glm/glm.hpp>

#include "../core/ecs/component/Component.hpp"

struct BoxColliderComponent : public Component<BoxColliderComponent> {
  COMPONENT_NAME("BoxColliderComponent");
  glm::vec2 halfExtents{0.0f};            // half width/height, like a radius from the center
  glm::vec2 offset{0.0f};                 // offset from the transform
  uint32_t collisionLayer = 1u << 0;      // bit mask denoting the layer the collider is on
  uint32_t collisionMask = 0xFFFF'FFFFu;  // bit mask denoting the layer(s) the collider collides with
  uint32_t blocksMask = 0;                // layers it stops moving through it (moveBlocked); 0: not solid
};
