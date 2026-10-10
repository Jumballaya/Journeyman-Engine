#pragma once

#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "../core/ecs/World.hpp"

// Moving an entity's collider through solid ones without entering them: the
// box version of TileGrid::move. A collider blocks a mover when its blocksMask
// meets the mover's layerMask; terrain on its layers blocks it too. Moves go
// along x, then y, stopping flush against the nearest blocker, so a blocked axis
// doesn't stop the other one (sliding along walls). A box or one-way platform
// it starts in doesn't block it, so it can get out; other terrain pushes it out first
// (unless it's solid itself, or that would put it in a solid).
struct BlockedMove {
  glm::ivec2 hit{0};               // -1/+1: the side blocked on each axis (hit.y < 0: standing on something)
  EntityId hitX = kNoEntityId;     // what stopped it along x
  EntityId hitY = kNoEntityId;     // and along y
  glm::vec2 normal{0.0f};          // the surface met along y, facing it (standing: the ground's, leaning on slopes)
};

// One frame of moves: who carried each rider across, and the way each body went
// (to sweep collisions along). Clear it each frame.
struct MoveFrame {
  std::unordered_map<EntityId, EntityId> carrier;
  std::unordered_map<EntityId, std::vector<glm::vec2>> paths;  // its position at each turn, from where it started

  // It went from `from` through `via` (in order). From elsewhere than its path's end, it jumped: starts anew.
  void went(EntityId entity, glm::vec2 from, const std::vector<glm::vec2>& via);
  const std::vector<glm::vec2>* pathOf(EntityId entity) const {
    const auto it = paths.find(entity);
    return it == paths.end() ? nullptr : &it->second;
  }
  void clear() {
    carrier.clear();
    paths.clear();
  }
};

// Moves `mover` (with a TransformComponent and BoxColliderComponent, and no
// parent) by `delta`, exactly: terrain is walls and floors like boxes. With
// `slide` > 0, a move blocked along one axis nudges up to `slide` units sideways
// toward an opening, so gaps are easy to enter (among boxes, not near terrain).
// A solid mover, or one with terrain, carries what stands on it (solid boxes only
// with a VelocityComponent, so not walls): rigidly across, each meeting walls on
// its own; up together, as far as all can go (what stops one is the mover's hitY);
// down after it. Carrying, it doesn't slide. A solid box mover pushes what it
// runs into that has a VelocityComponent (each body once a move); with nowhere
// to go, that stays in it (crushed). Moves sharing a `frame` carry each rider
// across with one platform: the first to (one on two lifts goes once); the
// frame keeps the way each body went.
BlockedMove moveBlocked(World& world, EntityId mover, glm::vec2 delta, float slide = 0.0f, MoveFrame* frame = nullptr);

// Moves it like moveBlocked, but walking: up slopes to 50° (steeper is a wall)
// and onto ledges up to 1 unit, down slopes and steps without leaving them
// (unless rising), onto one-way platforms from above (`dropThrough`: falls
// through them). Carries what stands on it the same way.
BlockedMove walkBlocked(World& world, EntityId mover, glm::vec2 delta, bool dropThrough = false, MoveFrame* frame = nullptr);

// What stands on `platform` (a solid box's top, or its terrain) on layers it
// holds: what its moves carry. Solid ones only if they move (have a velocity), not walls.
std::vector<EntityId> riders(World& world, EntityId platform);

// Whether `body` stands on `platform` so that moving it carries the body.
bool standsOn(World& world, EntityId body, EntityId platform);

// What `body` stands on (ground, wall top or platform), or kNoEntityId in the
// air; `dropThrough`: one-way platforms don't hold it.
EntityId supportOf(World& world, EntityId body, bool dropThrough = false);
