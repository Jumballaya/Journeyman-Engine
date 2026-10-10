#pragma once

#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "../core/ecs/World.hpp"
#include "Shapes.hpp"

// Asking the world where its colliders and terrain are: what a ray hits,
// what's in an area. One counts when its layerMask meets `mask`; ones about to
// be destroyed don't. Terrain answers as the entity whose ground it is.

struct RayHit {
  EntityId entity;
  glm::vec2 point;   // where the ray met it
  glm::vec2 normal;  // its surface there, facing the ray
  float distance;
};

// The first collider a ray from origin along direction (any length) meets
// within maxDistance (infinity: no limit), skipping `ignore` (the caster, say).
// A ray starting inside one hits it at 0. No direction, or NaN: no hit.
std::optional<RayHit> raycast(World& world, glm::vec2 origin, glm::vec2 direction, float maxDistance, uint32_t mask,
                              EntityId ignore = kNoEntityId);

// What overlaps `area`: colliders in forEachCollider's order (boxes, then
// circles), then terrain's entities; skipping `ignore`, an entity at most once.
std::vector<EntityId> overlapping(World& world, const Shape& area, uint32_t mask, EntityId ignore = kNoEntityId);
