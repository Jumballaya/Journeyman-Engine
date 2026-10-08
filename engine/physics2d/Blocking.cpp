#include "Blocking.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "BoxColliderComponent.hpp"
#include "TransformComponent.hpp"

namespace {

constexpr float kGap = 0.01f;  // left between a stopped box and its blocker, like the tilemap's

struct Box {
  EntityId entity;
  glm::vec2 center, half;
};

bool overlaps(glm::vec2 center, glm::vec2 half, const Box& b) {
  return std::abs(center.x - b.center.x) < half.x + b.half.x && std::abs(center.y - b.center.y) < half.y + b.half.y;
}

struct Mover {
  glm::vec2 center, half;
  const std::vector<Box>& solids;

  bool free(glm::vec2 at) const {
    return std::none_of(solids.begin(), solids.end(), [&](const Box& b) { return overlaps(at, half, b); });
  }

  // How far a box at `from` can go along `axis` (up to |delta|), and what stops it.
  float reach(glm::vec2 from, int axis, float delta, const Box** blocker) const {
    const int other = 1 - axis;
    float allowed = std::fabs(delta);
    *blocker = nullptr;
    for (const Box& b : solids) {
      if (std::abs(from[other] - b.center[other]) >= half[other] + b.half[other]) continue;  // beside its path
      const float gap = delta > 0.0f ? (b.center[axis] - b.half[axis]) - (from[axis] + half[axis])
                                     : (from[axis] - half[axis]) - (b.center[axis] + b.half[axis]);
      if (gap < 0.0f || gap > allowed) continue;  // behind it, or beyond the move
      if (*blocker && gap == allowed) continue;   // ties go to the first in world order
      allowed = gap;
      *blocker = &b;
    }
    return *blocker ? std::max(0.0f, allowed - kGap) : allowed;
  }

  void moveAxis(BlockedMove& m, int axis, float delta, float slide) {
    if (delta == 0.0f) return;
    const Box* blocker;
    const float distance = reach(center, axis, delta, &blocker);
    const float sign = delta > 0.0f ? 1.0f : -1.0f;
    center[axis] += sign * distance;
    if (!blocker) return;
    m.hit[axis] = static_cast<int>(sign);
    (axis == 0 ? m.hitX : m.hitY) = blocker->entity;

    // Nudge toward the nearest opening that would let the whole move through.
    const int other = 1 - axis;
    const float left = std::fabs(delta) - distance;
    if (left <= 0.0f) return;
    for (float off = 1.0f; off <= slide; off += 1.0f) {
      for (float side : {-1.0f, 1.0f}) {
        glm::vec2 probe = center;
        probe[other] += side * off;
        if (!free(probe)) continue;
        const Box* stop;
        reach(probe, axis, sign * left, &stop);
        if (stop) continue;
        center[other] += side * std::min(off, std::fabs(delta));
        return;
      }
    }
  }
};

}  // namespace

BlockedMove moveBlocked(World& world, EntityId mover, glm::vec2 delta, float slide) {
  BlockedMove m;
  auto* trans = world.getComponent<TransformComponent>(mover);
  auto* collider = world.getComponent<BoxColliderComponent>(mover);
  if (!trans) return m;
  if (!std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(slide)) return m;
  if (!collider) {  // nothing to block
    trans->position.x += delta.x;
    trans->position.y += delta.y;
    return m;
  }
  const glm::vec2 start = glm::vec2(trans->position) + collider->offset, half = collider->halfExtents;
  // Only what the move could reach: the box around its whole travel, slide included.
  const glm::vec2 reachMin = glm::min(start, start + delta) - half - glm::vec2(slide + 1.0f);
  const glm::vec2 reachMax = glm::max(start, start + delta) + half + glm::vec2(slide + 1.0f);
  std::vector<Box> solids;
  for (auto [entity, t, c] : world.view<TransformComponent, BoxColliderComponent>()) {
    if (entity == mover || !(c->blocksMask & collider->layerMask) || world.isPendingDestroy(entity)) continue;
    const Box box{entity, glm::vec2(t->position) + c->offset, c->halfExtents};
    if (glm::any(glm::greaterThanEqual(box.center - box.half, reachMax)) ||
        glm::any(glm::lessThanEqual(box.center + box.half, reachMin)))
      continue;
    if (!overlaps(start, half, box)) solids.push_back(box);  // already inside one: free to leave it
  }
  Mover body{start, half, solids};
  body.moveAxis(m, 0, delta.x, delta.y == 0.0f ? slide : 0.0f);
  body.moveAxis(m, 1, delta.y, delta.x == 0.0f ? slide : 0.0f);
  trans->position.x += body.center.x - start.x;
  trans->position.y += body.center.y - start.y;
  return m;
}
