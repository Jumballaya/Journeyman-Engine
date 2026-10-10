#include "Blocking.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>
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

// In a world with drawn ground: what blocks is edges, terrain's lines and solid boxes' sides.
struct Edge {
  glm::vec2 a, b;
  EntityId entity;
  bool oneWay;    // held only from above
  bool walkable;  // no steeper than 50°: a floor or ceiling, not a wall
};

constexpr float kStep = 1.0f;  // what a walker steps up onto, any shape

struct Span {
  float lo, hi;
};

// The range of the edge's other coordinate where its `axis` coordinate is in (from, to); nothing if never.
std::optional<Span> span(const Edge& e, int axis, float from, float to) {
  const int other = 1 - axis;
  const float a = e.a[axis], d = e.b[axis] - e.a[axis];
  if (d == 0.0f) {
    if (a <= from || a >= to) return std::nullopt;
    return Span{std::min(e.a[other], e.b[other]), std::max(e.a[other], e.b[other])};
  }
  float t0 = (from - a) / d, t1 = (to - a) / d;
  if (t0 > t1) std::swap(t0, t1);
  t0 = std::max(t0, 0.0f);
  t1 = std::min(t1, 1.0f);
  if (t0 >= t1) return std::nullopt;
  const float v0 = e.a[other] + (e.b[other] - e.a[other]) * t0, v1 = e.a[other] + (e.b[other] - e.a[other]) * t1;
  return Span{std::min(v0, v1), std::max(v0, v1)};
}

glm::vec2 upward(const Edge& e) {
  const glm::vec2 n = glm::normalize(glm::vec2(e.a.y - e.b.y, e.b.x - e.a.x));
  return n.y < 0.0f ? -n : n;
}

// The edge's height at x, or at its end nearest x (a wall's: its lower end).
float heightAt(const Edge& e, float x) {
  if (e.a.x == e.b.x) return std::min(e.a.y, e.b.y);
  const float t = std::clamp((x - e.a.x) / (e.b.x - e.a.x), 0.0f, 1.0f);
  return e.a.y + (e.b.y - e.a.y) * t;
}

struct Stop {
  float distance;
  const Edge* edge;  // null: nothing in the way
};

// A box walking among edges: along x, stopped exactly by walls and stepping
// up onto floors (slopes to 50°, ledges to kStep); along y exactly.
struct Walker {
  glm::vec2 center, half;
  const std::vector<Edge>& edges;
  bool dropThrough;

  // How far it can go along x by dx before a wall: an edge in its side that it
  // can't step onto (a floor or ledge it meets within kStep of its feet).
  Stop sweepX(float dx) const {
    const float feet = center.y - half.y, top = center.y + half.y, lead = center.x + (dx > 0.0f ? half.x : -half.x);
    Stop stop{std::fabs(dx), nullptr};
    for (const Edge& e : edges) {
      const float meets = e.walkable ? heightAt(e, lead) : std::max(e.a.y, e.b.y);
      if (e.oneWay || meets <= feet + kStep) continue;
      const auto s = span(e, 1, feet, top);
      if (!s) continue;
      const float gap = dx > 0.0f ? s->lo - (center.x + half.x) : (center.x - half.x) - s->hi;
      if (gap < -kGap || gap > stop.distance || (stop.edge && gap == stop.distance)) continue;  // in it, or beyond
      stop = {std::max(gap, 0.0f), &e};
    }
    if (stop.edge) stop.distance = std::max(0.0f, stop.distance - kGap);
    return stop;
  }

  // How far a box at `at` can go along y by dy, and what stops it. Edges it's in don't.
  Stop sweepY(glm::vec2 at, float dy, bool oneWays = true) const {
    const float left = at.x - half.x, right = at.x + half.x;
    Stop stop{std::fabs(dy), nullptr};
    for (const Edge& e : edges) {
      if (e.oneWay && (dy > 0.0f || dropThrough || !oneWays)) continue;
      const auto s = span(e, 0, left, right);
      if (!s) continue;
      const float gap = dy < 0.0f ? (at.y - half.y) - s->hi : s->lo - (at.y + half.y);
      if (gap < -kGap || gap > stop.distance + (stop.edge ? kGap : 0.0f)) continue;  // in it, or beyond
      // Meeting two at once (a slope's foot), it's on the flatter.
      if (stop.edge && gap > stop.distance - kGap && upward(e).y <= upward(*stop.edge).y) continue;
      stop = {std::max(0.0f, std::min(gap, stop.distance)), &e};
    }
    if (stop.edge) stop.distance = std::max(0.0f, stop.distance - kGap);
    return stop;
  }

  // How far up a box at `at` goes to stand on what it's in (one-ways too, if it
  // can get on them): none if too far, under a roof, or with nothing walkable there.
  struct Climb {
    std::optional<float> lift;
    const Edge* by;  // the highest edge it's in
  };
  Climb climbAt(glm::vec2 at, float climb, bool oneWays) const {
    Climb c{0.0f, nullptr};
    for (const Edge& e : edges) {
      if ((e.oneWay && (!oneWays || dropThrough)) || !overlapsSegment(Shape::box(at, half), e.a, e.b)) continue;
      const auto s = span(e, 0, at.x - half.x, at.x + half.x);
      const float need = s ? s->hi - (at.y - half.y) + kGap : INFINITY;
      if (e.oneWay && need > climb) continue;  // passing through it
      if (!c.by || need > *c.lift) c = {need, &e};
    }
    if (!c.by) return c;
    const glm::vec2 up = at + glm::vec2(0.0f, *c.lift);
    const Stop ground = sweepY(up, -2.0f * kGap, oneWays);
    if (*c.lift > climb || sweepY(at, *c.lift).edge || !ground.edge || !ground.edge->walkable) c.lift.reset();
    return c;
  }

  void walkX(BlockedMove& m, float dx) {
    if (dx == 0.0f) return;
    // Steps for following the ground; walls are swept exactly.
    const float maxStep = std::clamp(half.x, 0.05f, 4.0f), dir = dx > 0.0f ? 1.0f : -1.0f;
    const int steps = std::clamp(static_cast<int>(std::ceil(std::fabs(dx) / maxStep)), 1, 1024);
    const float step = std::fabs(dx) / static_cast<float>(steps), climb = step * kClimb + kStep;
    for (int i = 0; i < steps; ++i) {
      const Stop wall = sweepX(dir * step);
      const glm::vec2 next = center + glm::vec2(dir * wall.distance, 0.0f);
      Climb c = climbAt(next, climb, true);
      if (!c.lift) c = climbAt(next, climb, false);  // through one-ways it can't get on
      const Edge* by = wall.edge;
      if (c.lift) {
        center = next + glm::vec2(0.0f, *c.lift);
      } else {  // can't get on what's ahead: as far toward it as it can climb
        float lo = 0.0f, hi = wall.distance, lift = 0.0f;
        for (int k = 0; k < 12; ++k) {
          const float mid = (lo + hi) * 0.5f;
          const Climb part = climbAt(center + glm::vec2(dir * mid, 0.0f), climb, false);
          if (part.lift) std::tie(lo, lift) = std::pair(mid, *part.lift);
          else hi = mid;
        }
        center += glm::vec2(dir * lo, lift);
        by = c.by;
      }
      if (!by) continue;
      m.hit.x = static_cast<int>(dir);
      m.hitX = by->entity;
      return;
    }
  }

  void walkY(BlockedMove& m, float dy) {
    if (dy == 0.0f) return;
    land(m, sweepY(center, dy), dy > 0.0f ? 1.0f : -1.0f);
  }

  // Down onto the ground within `depth`, if there is any.
  void snapDown(BlockedMove& m, float depth) {
    const Stop stop = sweepY(center, -depth);
    if (stop.edge) land(m, stop, -1.0f);
  }

 private:
  void land(BlockedMove& m, Stop stop, float sign) {
    center.y += sign * stop.distance;
    if (!stop.edge) return;
    m.hit.y = static_cast<int>(sign);
    m.hitY = stop.edge->entity;
    m.normal = sign < 0.0f ? upward(*stop.edge) : -upward(*stop.edge);  // facing the body
  }
};

bool hasTerrain(World& world, uint32_t mask) {
  for (auto [entity, terrain] : world.view<TerrainComponent>())
    if ((terrain->layerMask & mask) && !terrain->chains.empty()) return true;
  return false;
}

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
  glm::vec2 end;
  if (!hasTerrain(world, collider->layerMask)) {
    Mover body{start, half, solids};
    body.moveAxis(m, 0, delta.x, delta.y == 0.0f ? slide : 0.0f);
    body.moveAxis(m, 1, delta.y, delta.x == 0.0f ? slide : 0.0f);
    if (m.hit.y != 0) m.normal = glm::vec2(0.0f, -static_cast<float>(m.hit.y));
    end = body.center;
  } else {
    std::vector<Edge> edges;
    forEachTerrainSegment(world, reachMin, reachMax, collider->layerMask, [&](const TerrainSegment& t) {
      if (t.entity == mover || t.a == t.b) return;
      if (!t.oneWay && overlapsSegment(Shape::box(start, half), t.a, t.b)) return;  // already in it: free to leave
      edges.push_back({t.a, t.b, t.entity, t.oneWay, std::fabs(t.b.y - t.a.y) <= kClimb * std::fabs(t.b.x - t.a.x)});
    });
    for (const Box& b : solids) {
      const glm::vec2 lo = b.center - b.half, hi = b.center + b.half, lr(hi.x, lo.y), ul(lo.x, hi.y);
      for (const auto& [a, z] : {std::pair(lo, lr), std::pair(lr, hi), std::pair(hi, ul), std::pair(ul, lo)})
        edges.push_back({a, z, b.entity, false, a.y == z.y});  // tops and bottoms are floors and ceilings
    }
    Walker body{start, glm::max(half, glm::vec2(kGap)), edges, dropThrough};  // a point would slip between edges
    const bool grounded = body.sweepY(start, -4.0f * kGap).edge != nullptr;
    body.walkX(m, delta.x);
    body.walkY(m, delta.y);
    // Walking downhill (or over a bump) stays on the ground rather than leaving it a little each frame.
    const float travelled = std::fabs(body.center.x - start.x);
    if (grounded && travelled > 0.0f && delta.y <= 0.0f && m.hit.y == 0) body.snapDown(m, travelled * kClimb + 4.0f * kGap);
    end = body.center;
  }
  trans->position.x += end.x - start.x;
  trans->position.y += end.y - start.y;
  return m;
}
