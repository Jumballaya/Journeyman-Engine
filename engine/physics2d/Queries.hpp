#pragma once

#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "../core/ecs/World.hpp"
#include "Shapes.hpp"

// Asking the world where its colliders are: what a ray hits, what's in an
// area. A collider counts when its layerMask meets `mask`; ones about to be
// destroyed don't.

struct RayHit {
  EntityId entity;
  glm::vec2 point;   // where the ray met it
  glm::vec2 normal;  // its surface there, facing the ray
  float distance;
};

// The first collider a ray from origin along direction meets within
// maxDistance, skipping `ignore` (the caster, say). No direction: no hit.
std::optional<RayHit> raycast(World& world, glm::vec2 origin, glm::vec2 direction, float maxDistance, uint32_t mask,
                              EntityId ignore = kNoEntityId);

// The colliders overlapping `area`, in world order (boxes, then circles); an
// entity at most once.
std::vector<EntityId> overlapping(World& world, const Shape& area, uint32_t mask);
