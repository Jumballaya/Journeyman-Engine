#include "Blocking.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "BoxColliderComponent.hpp"
#include "Shapes.hpp"
#include "Terrain.hpp"
#include "TransformComponent.hpp"

namespace {

constexpr float kGap = 0.01f;    // left between a stopped box and its blocker, like the tilemap's
constexpr float kClimb = 1.192f;  // tan 50°: the steepest walkable ground, rise per unit run

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

// Near terrain: what blocks is edges, terrain's lines and solid boxes' sides.
struct Edge {
  glm::vec2 a, b;
  EntityId entity;
  bool oneWay;     // held only from above
  bool walkable;  // terrain no steeper than kClimb: walked up (a box's sides aren't)
};

struct Span {
  float lo, hi;
};

// The heights an edge has over x in (left, right); nothing if none of it is.
std::optional<Span> span(const Edge& e, float left, float right) {
  if (e.a.x == e.b.x) {
    if (e.a.x <= left || e.a.x >= right) return std::nullopt;
    return Span{std::min(e.a.y, e.b.y), std::max(e.a.y, e.b.y)};
  }
  float t0 = (left - e.a.x) / (e.b.x - e.a.x), t1 = (right - e.a.x) / (e.b.x - e.a.x);
  if (t0 > t1) std::swap(t0, t1);
  t0 = std::max(t0, 0.0f);
  t1 = std::min(t1, 1.0f);
  if (t0 >= t1) return std::nullopt;
  const float y0 = e.a.y + (e.b.y - e.a.y) * t0, y1 = e.a.y + (e.b.y - e.a.y) * t1;
  return Span{std::min(y0, y1), std::max(y0, y1)};
}

glm::vec2 upward(const Edge& e) {
  const glm::vec2 n = glm::normalize(glm::vec2(e.a.y - e.b.y, e.b.x - e.a.x));
  return n.y < 0.0f ? -n : n;
}

// A box walking among edges: along x in small steps, up slopes it can climb
// and stopping at walls; along y exactly, landing on floors and heading into ceilings.
struct Walker {
  glm::vec2 center, half;
  const std::vector<Edge>& edges;
  bool dropThrough;

  bool blockedAt(glm::vec2 at, const Edge** by = nullptr) const {
    for (const Edge& e : edges) {
      if (e.oneWay || !overlapsSegment(Shape::box(at, half), e.a, e.b)) continue;
      if (by) *by = &e;
      return true;
    }
    return false;
  }

  void walkX(BlockedMove& m, float dx) {
    if (dx == 0.0f) return;
    const int steps = std::clamp(static_cast<int>(std::ceil(std::fabs(dx) / std::clamp(half.x, 1.0f, 4.0f))), 1, 256);
    const float step = dx / static_cast<float>(steps), climb = std::fabs(step) * kClimb + 2.0f * kGap;
    for (int i = 0; i < steps; ++i) {
      const glm::vec2 next = center + glm::vec2(step, 0.0f);
      // What it walks into, and how far up it would have to go to be on top of it all.
      float lift = 0.0f;
      bool into = false, walkable = true;
      for (const Edge& e : edges) {
        if (!overlapsSegment(Shape::box(next, half), e.a, e.b)) continue;
        const auto s = span(e, next.x - half.x, next.x + half.x);
        const float need = s ? s->hi - (next.y - half.y) + kGap : INFINITY;
        if (e.oneWay && need > climb) continue;  // passing through it
        lift = std::max(lift, need);
        into = true;
        walkable = walkable && e.walkable;
      }
      const glm::vec2 lifted = next + glm::vec2(0.0f, lift);
      if (!into) {
        center = next;
      } else if (walkable && lift <= climb && !blockedAt(lifted)) {
        center = lifted;
      } else if (const Edge* wall = nullptr; blockedAt(next, &wall)) {
        float lo = 0.0f, hi = 1.0f;  // as far as it fits, then stop
        for (int k = 0; k < 12; ++k) (blockedAt(center + glm::vec2(step * (lo + hi) * 0.5f, 0.0f)) ? hi : lo) = (lo + hi) * 0.5f;
        center.x += step * lo;
        m.hit.x = dx > 0.0f ? 1 : -1;
        m.hitX = wall->entity;
        return;
      } else {
        center = next;  // a one-way it can't get on top of: through it
      }
    }
  }

  struct Stop {
    float distance;
    const Edge* edge;
  };
  // How far it can go along y by dy, and what stops it. Edges it's already in don't.
  Stop sweepY(float dy) const {
    const float left = center.x - half.x, right = center.x + half.x;
    Stop stop{std::fabs(dy), nullptr};
    for (const Edge& e : edges) {
      if (e.oneWay && (dy > 0.0f || dropThrough)) continue;
      const auto s = span(e, left, right);
      if (!s) continue;
      const float gap = dy < 0.0f ? (center.y - half.y) - s->hi : s->lo - (center.y + half.y);
      if (gap < -kGap || gap > stop.distance + (stop.edge ? kGap : 0.0f)) continue;  // in it (free to leave), or beyond
      // Meeting two at once (a slope's foot), it's on the flatter.
      if (stop.edge && gap > stop.distance - kGap && upward(e).y <= upward(*stop.edge).y) continue;
      stop = {std::max(0.0f, std::min(gap, stop.distance)), &e};
    }
    if (stop.edge) stop.distance = std::max(0.0f, stop.distance - kGap);
    return stop;
  }

  // Moves along y by up to dy; with `onlyToLand`, only if it lands within that (a snap down to the ground).
  void walkY(BlockedMove& m, float dy, bool onlyToLand = false) {
    if (dy == 0.0f) return;
    const Stop stop = sweepY(dy);
    if (onlyToLand && !stop.edge) return;
    const float sign = dy > 0.0f ? 1.0f : -1.0f;
    center.y += sign * stop.distance;
    if (!stop.edge) return;
    m.hit.y = static_cast<int>(sign);
    m.hitY = stop.edge->entity;
    m.normal = sign < 0.0f ? upward(*stop.edge) : -upward(*stop.edge);
  }
};

}  // namespace

BlockedMove moveBlocked(World& world, EntityId mover, glm::vec2 delta, float slide, bool dropThrough) {
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
  // Only what the move could reach: the box around its whole travel, slide and climbing included.
  const glm::vec2 pad(slide + 1.0f, slide + 1.0f + std::fabs(delta.x) * kClimb);
  const glm::vec2 reachMin = glm::min(start, start + delta) - half - pad, reachMax = glm::max(start, start + delta) + half + pad;
  std::vector<Box> solids;
  for (auto [entity, t, c] : world.view<TransformComponent, BoxColliderComponent>()) {
    if (entity == mover || !(c->blocksMask & collider->layerMask) || world.isPendingDestroy(entity)) continue;
    const Box box{entity, glm::vec2(t->position) + c->offset, c->halfExtents};
    if (glm::any(glm::greaterThanEqual(box.center - box.half, reachMax)) ||
        glm::any(glm::lessThanEqual(box.center + box.half, reachMin)))
      continue;
    if (!overlaps(start, half, box)) solids.push_back(box);  // already inside one: free to leave it
  }
  std::vector<Edge> edges;
  forEachTerrainSegment(world, reachMin, reachMax, collider->layerMask, [&](const TerrainSegment& t) {
    if (t.entity == mover) return;
    if (!t.oneWay && overlapsSegment(Shape::box(start, half), t.a, t.b)) return;  // already in it: free to leave
    edges.push_back({t.a, t.b, t.entity, t.oneWay, std::fabs(t.b.y - t.a.y) <= kClimb * std::fabs(t.b.x - t.a.x)});
  });
  glm::vec2 end;
  if (edges.empty()) {
    Mover body{start, half, solids};
    body.moveAxis(m, 0, delta.x, delta.y == 0.0f ? slide : 0.0f);
    body.moveAxis(m, 1, delta.y, delta.x == 0.0f ? slide : 0.0f);
    if (m.hit.y != 0) m.normal = glm::vec2(0.0f, -static_cast<float>(m.hit.y));
    end = body.center;
  } else {
    for (const Box& b : solids) {
      const glm::vec2 lo = b.center - b.half, hi = b.center + b.half;
      for (const auto& [a, z] : {std::pair(lo, glm::vec2(hi.x, lo.y)), std::pair(glm::vec2(hi.x, lo.y), hi),
                                 std::pair(hi, glm::vec2(lo.x, hi.y)), std::pair(glm::vec2(lo.x, hi.y), lo)})
        edges.push_back({a, z, b.entity, false, false});
    }
    Walker body{start, half, edges, dropThrough};
    const bool grounded = body.sweepY(-4.0f * kGap).edge != nullptr;
    body.walkX(m, delta.x);
    body.walkY(m, delta.y);
    // Walking downhill (or over a bump) stays on the ground rather than leaving it a little each frame.
    if (grounded && delta.x != 0.0f && delta.y <= 0.0f && m.hit.y == 0)
      body.walkY(m, -(std::fabs(delta.x) * kClimb + 4.0f * kGap), true);
    end = body.center;
  }
  trans->position.x += end.x - start.x;
  trans->position.y += end.y - start.y;
  return m;
}
