#pragma once

#include <glm/glm.hpp>

#include "../core/ecs/component/Component.hpp"

// A round collider: reports overlaps as BoxColliderComponent does, for shapes
// a box fits badly (a barrel, a blob, a swing's reach). Not solid to movers.
struct CircleColliderComponent : public Component<CircleColliderComponent> {
  COMPONENT_NAME("CircleColliderComponent");
  float radius = 8.0f;
  glm::vec2 offset{0.0f};                 // from the transform
  uint32_t collisionLayer = 1u << 0;      // the layers it's on
  uint32_t collisionMask = 0xFFFF'FFFFu;  // the layers it wants to touch
};
