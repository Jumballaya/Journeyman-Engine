#include "Queries.hpp"

#include <algorithm>
#include <cmath>

#include "Colliders.hpp"
#include "Terrain.hpp"

namespace {

bool finite(glm::vec2 v) { return std::isfinite(v.x) && std::isfinite(v.y); }

// direction at unit length, scaled first so a huge or tiny one doesn't
// overflow or vanish on the way; nothing when it has no (finite) length.
std::optional<glm::vec2> unit(glm::vec2 direction) {
  const float scale = std::max(std::abs(direction.x), std::abs(direction.y));
  if (!finite(direction) || !(scale > 0.0f)) return std::nullopt;
  return glm::normalize(direction / scale);
}

}  // namespace

std::optional<RayHit> raycast(World& world, glm::vec2 origin, glm::vec2 direction, float maxDistance, uint32_t mask,
                              EntityId ignore) {
  const auto along = unit(direction);
  if (!along || !finite(origin) || std::isnan(maxDistance) || maxDistance < 0.0f) return std::nullopt;
  std::optional<RayHit> nearest;
  auto keep = [&](EntityId entity, std::optional<ShapeHit> hit) {
    if (hit && (!nearest || hit->distance < nearest->distance))  // ties: the first met (colliders, then terrain)
      nearest = RayHit{entity, origin + *along * hit->distance, hit->normal, hit->distance};
  };
  forEachCollider(world, [&](const Collider& c) {
    if (c.entity != ignore && (c.layerMask & mask))
      keep(c.entity, raycast(c.shape, origin, *along, nearest ? nearest->distance : maxDistance));
  });
  // Only ground the ray can reach: the box around it (an endless one, all of it).
  glm::vec2 lo(-INFINITY), hi(INFINITY);
  if (std::isfinite(maxDistance)) {
    const glm::vec2 end = origin + *along * maxDistance;
    lo = glm::min(origin, end);
    hi = glm::max(origin, end);
  }
  forEachTerrainSegment(world, lo, hi, mask, [&](const TerrainSegment& t) {
    if (t.entity != ignore) keep(t.entity, raycastSegment(t.a, t.b, t.oneWay, origin, *along, nearest ? nearest->distance : maxDistance));
  });
  return nearest;
}

std::vector<EntityId> overlapping(World& world, const Shape& area, uint32_t mask, EntityId ignore) {
  std::vector<EntityId> found;
  if (!finite(area.center) || !finite(area.extent())) return found;
  auto add = [&](EntityId entity) {
    if (entity != ignore && std::find(found.begin(), found.end(), entity) == found.end()) found.push_back(entity);
  };
  forEachCollider(world, [&](const Collider& c) {
    if ((c.layerMask & mask) && overlaps(area, c.shape)) add(c.entity);
  });
  forEachTerrainSegment(world, area.center - area.extent(), area.center + area.extent(), mask, [&](const TerrainSegment& t) {
    if (overlapsSegment(area, t.a, t.b)) add(t.entity);
  });
  return found;
}
