#include "Queries.hpp"

#include <algorithm>
#include <cmath>

#include "BoxColliderComponent.hpp"
#include "CircleColliderComponent.hpp"
#include "TransformComponent.hpp"

namespace {

// Calls visit(entity, shape) for each collider on `mask`'s layers.
template <typename Visit>
void eachCollider(World& world, uint32_t mask, Visit visit) {
  for (auto [entity, trans, box] : world.view<TransformComponent, BoxColliderComponent>()) {
    if ((box->layerMask & mask) && !world.isPendingDestroy(entity))
      visit(entity, Shape::box(glm::vec2(trans->position) + box->offset, box->halfExtents));
  }
  for (auto [entity, trans, circle] : world.view<TransformComponent, CircleColliderComponent>()) {
    if ((circle->layerMask & mask) && !world.isPendingDestroy(entity))
      visit(entity, Shape::circle(glm::vec2(trans->position) + circle->offset, circle->radius));
  }
}

}  // namespace

std::optional<RayHit> raycast(World& world, glm::vec2 origin, glm::vec2 direction, float maxDistance, uint32_t mask,
                              EntityId ignore) {
  const float length = glm::length(direction);
  if (!(length > 0.0f) || !std::isfinite(length) || !std::isfinite(maxDistance) || maxDistance < 0.0f) return std::nullopt;
  direction /= length;
  std::optional<RayHit> nearest;
  eachCollider(world, mask, [&](EntityId entity, const Shape& shape) {
    if (entity == ignore) return;
    const float within = nearest ? nearest->distance : maxDistance;
    auto hit = raycast(shape, origin, direction, within);
    if (hit && (!nearest || hit->distance < nearest->distance))  // ties: the first in world order
      nearest = RayHit{entity, origin + direction * hit->distance, hit->normal, hit->distance};
  });
  return nearest;
}

std::vector<EntityId> overlapping(World& world, const Shape& area, uint32_t mask) {
  std::vector<EntityId> found;
  eachCollider(world, mask, [&](EntityId entity, const Shape& shape) {
    if (overlaps(area, shape) && std::find(found.begin(), found.end(), entity) == found.end()) found.push_back(entity);
  });
  return found;
}
