#include "Queries.hpp"

#include <algorithm>
#include <cmath>

#include "Colliders.hpp"

namespace {

bool finite(glm::vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }

// direction at unit length, scaled first so a huge or tiny one doesn't
// overflow or vanish on the way; nothing when it has no length.
std::optional<glm::vec2> unit(glm::vec2 direction) {
  const float scale = std::max(std::abs(direction.x), std::abs(direction.y));
  if (!(scale > 0.0f) || !std::isfinite(scale)) return std::nullopt;
  return glm::normalize(direction / scale);
}

}  // namespace

std::optional<RayHit> raycast(World& world, glm::vec2 origin, glm::vec2 direction, float maxDistance, uint32_t mask,
                              EntityId ignore) {
  const auto along = unit(direction);
  if (!along || !finite(origin) || std::isnan(maxDistance) || maxDistance < 0.0f) return std::nullopt;
  std::optional<RayHit> nearest;
  forEachCollider(world, [&](const Collider& c) {
    if (c.entity == ignore || !(c.layerMask & mask)) return;
    auto hit = raycast(c.shape, origin, *along, nearest ? nearest->distance : maxDistance);
    if (hit && (!nearest || hit->distance < nearest->distance))  // ties: the first in forEachCollider's order
      nearest = RayHit{c.entity, origin + *along * hit->distance, hit->normal, hit->distance};
  });
  return nearest;
}

std::vector<EntityId> overlapping(World& world, const Shape& area, uint32_t mask, EntityId ignore) {
  std::vector<EntityId> found;
  if (!finite(area.center) || !finite(area.extent())) return found;
  forEachCollider(world, [&](const Collider& c) {
    if (c.entity == ignore || !(c.layerMask & mask) || !overlaps(area, c.shape)) return;
    if (std::find(found.begin(), found.end(), c.entity) == found.end()) found.push_back(c.entity);
  });
  return found;
}
