#pragma once

#include <glm/glm.hpp>

#include "../core/ecs/World.hpp"

// Moving an entity's collider through solid ones without entering them: the
// box version of TileGrid::move. A collider blocks a mover when its blocksMask
// meets the mover's layerMask. Moves go along x, then y, stopping flush
// against the nearest blocker, so a blocked axis doesn't stop the other one
// (sliding along walls). Colliders it starts out overlapping don't block it,
// so it can always get out of one. Exact: no steps, nothing is skipped.
struct BlockedMove {
  glm::ivec2 hit{0};               // -1/+1: the side blocked on each axis (hit.y < 0: standing on something)
  EntityId hitX = kNoEntityId;     // what stopped it along x
  EntityId hitY = kNoEntityId;     // and along y
};

// Moves `mover` (with a TransformComponent and BoxColliderComponent) by
// `delta`. With `slide` > 0, a move blocked along one axis nudges up to `slide`
// units sideways toward an opening, so gaps are easy to enter. For entities
// without a parent (a child's transform follows its parent's).
BlockedMove moveBlocked(World& world, EntityId mover, glm::vec2 delta, float slide = 0.0f);
