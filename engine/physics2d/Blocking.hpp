#pragma once

#include <glm/glm.hpp>

#include "../core/ecs/World.hpp"

// Moving an entity's collider through solid ones without entering them: the
// box version of TileGrid::move. A collider blocks a mover when its blocksMask
// meets the mover's layerMask. Moves go along x, then y, stopping flush
// against the nearest blocker, so a blocked axis doesn't stop the other one
// (sliding along walls). Colliders it starts out overlapping don't block it,
// so it can always get out of one. Walls and floors stop it exactly.
//
// In a world with terrain on the mover's layers, it walks: up slopes to 50°
// (steeper is a wall) and onto ledges up to 1 unit, down slopes without leaving
// them, onto one-way platforms from above. Solid boxes are walls and floors.
struct BlockedMove {
  glm::ivec2 hit{0};               // -1/+1: the side blocked on each axis (hit.y < 0: standing on something)
  EntityId hitX = kNoEntityId;     // what stopped it along x
  EntityId hitY = kNoEntityId;     // and along y
  glm::vec2 normal{0.0f};          // the surface met along y, facing it (standing: the ground's, leaning on slopes)
};

// Moves `mover` (with a TransformComponent and BoxColliderComponent, and no
// parent) by `delta`. With `slide` > 0, a move blocked along one axis nudges up
// to `slide` units sideways toward an opening, so gaps are easy to enter (in
// worlds without terrain). `dropThrough` falls through one-way platforms.
// A solid mover, or one with terrain, carries what stands on it (solid boxes only
// with a VelocityComponent, so not walls): rigidly across, each meeting walls on
// its own; up together, as far as all can go (what stops one is the mover's hitY);
// down after it. Carrying, it doesn't slide.
BlockedMove moveBlocked(World& world, EntityId mover, glm::vec2 delta, float slide = 0.0f, bool dropThrough = false);
