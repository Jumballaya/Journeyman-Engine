#pragma once

#include <glm/glm.hpp>

#include "../core/ecs/component/Component.hpp"

struct BoxColliderComponent : public Component<BoxColliderComponent> {
  COMPONENT_NAME("BoxColliderComponent");
  glm::vec2 halfExtents{0.0f};               // half width/height, like a radius from the center
  glm::vec2 offset{0.0f};                    // offset from the transform
  uint32_t layerMask = 1u << 0;              // bit mask denoting the layer the collider is on
  uint32_t collidesWithMask = 0xFFFF'FFFFu;  // bit mask denoting the layer(s) the collider collides with
};

struct PODBoxColliderComponent {
  float hx, hy;
  float ox, oy;
  uint32_t layerMask;
  uint32_t collidesWithMask;
};

static_assert(std::is_trivially_copyable_v<PODBoxColliderComponent>, "POD must be trivially copyable");
static_assert(sizeof(PODBoxColliderComponent) == 24);