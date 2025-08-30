#pragma once

#include <glm/glm.hpp>

#include "../core/ecs/entity/EntityId.hpp"
#include "../core/events/EventType.hpp"

inline constexpr EventType EVT_Physics2DCollision = createEventType("physics2d.collision");

namespace events {

struct Physics2DCollision {
  EntityId a;
  EntityId b;
  float nx, ny;  // normal pointing in the direction of the collision from A -> B
};

};  // namespace events