#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "../core/ecs/component/Component.hpp"

// How a velocity moves its entity: straight through everything, or through
// solids and drawn ground the way entity.move() or entity.walk() would.
enum Motion : uint32_t { kFreeMotion = 0, kMoveMotion = 1, kWalkMotion = 2 };

// World units per second, changing by `acceleration` per second (gravity).
struct VelocityComponent : public Component<VelocityComponent> {
  COMPONENT_NAME("VelocityComponent");
  glm::vec2 velocity{0.0f};
  glm::vec2 acceleration{0.0f};
  uint32_t motion = kFreeMotion;  // a Motion (others move freely); move/walk need a box or terrain, and no parent
  uint32_t dropThrough = 0;       // walking: falls through one-way platforms while nonzero
  glm::vec2 blocked{0.0f};        // last step's blocked sides, -1/+1 (y < 0: on the ground); floats for scripts
  glm::vec2 travel{0.0f};         // how far it went last step
};
