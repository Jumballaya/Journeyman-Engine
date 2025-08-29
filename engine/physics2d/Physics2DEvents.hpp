#pragma once

#include "../core/ecs/entity/EntityId.hpp"
#include "../core/events/EventType.hpp"

inline constexpr EventType EVT_Physics2DCollision = createEventType("physics2d.collision");

namespace events {

struct Physics2DCollision {
  EntityId a;
  EntityId b;
};

};  // namespace events