#include "Blocking.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>
#include <tuple>
#include <vector>

#include "BoxColliderComponent.hpp"
#include "Shapes.hpp"
#include "Terrain.hpp"
#include "TransformComponent.hpp"
#include "VelocityComponent.hpp"

namespace {

constexpr float kGap = 0.01f;    // left between a stopped box and its blocker, like the tilemap's
constexpr float kClimb = 1.192f;  // tan 50°: the steepest walkable ground, rise per unit run
constexpr float kStanding = 4.0f * kGap;  // feet this close above ground stand on it

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
  std::vector<glm::vec2> path{};  // where it turned, and where it ended

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
    path.push_back(center);
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
        path.push_back(center);
        return;
      }
    }
  }
};

// What blocks a Walker: terrain's lines and solid boxes' sides.
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
  bool climbs;  // or goes rigidly: carried, or an exact move()
  std::vector<glm::vec2> path{};  // where it was after each step

  // How far it can go along x by dx before a wall: an edge in its side that it
  // can't step onto (a floor or ledge it meets within kStep of its feet).
  Stop sweepX(float dx) const {
    const float feet = center.y - half.y, top = center.y + half.y, lead = center.x + (dx > 0.0f ? half.x : -half.x);
    Stop stop{std::fabs(dx), nullptr};
    for (const Edge& e : edges) {
      const float meets = e.walkable ? heightAt(e, lead) : std::max(e.a.y, e.b.y);
      if (e.oneWay || (climbs && meets <= feet + kStep)) continue;
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

  // Not rising, it keeps to the ground at each step (down slopes, and steps
  // as high as it climbs), until it walks off it.
  void walkX(BlockedMove& m, float dx, float dy) {
    if (dx == 0.0f) return;
    if (!climbs) {
      const Stop wall = sweepX(dx);
      center.x += dx > 0.0f ? wall.distance : -wall.distance;
      path.push_back(center);
      if (wall.edge) std::tie(m.hit.x, m.hitX) = std::pair(dx > 0.0f ? 1 : -1, wall.edge->entity);
      return;
    }
    // Steps for following the ground; walls are swept exactly.
    const float maxStep = std::clamp(half.x, 0.05f, 4.0f), dir = dx > 0.0f ? 1.0f : -1.0f;
    const int steps = std::clamp(static_cast<int>(std::ceil(std::fabs(dx) / maxStep)), 1, 1024);
    const float step = std::fabs(dx) / static_cast<float>(steps), climb = step * kClimb + kStep;
    bool hugs = dy <= 0.0f && sweepY(center, -kStanding).edge;
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
      if (hugs) {
        const Stop ground = sweepY(center, -(climb + kStanding));
        hugs = ground.edge && ground.edge->walkable;
        if (hugs) land(m, ground, -1.0f);
        else std::tie(m.hit.y, m.hitY, m.normal) = std::tuple(0, kNoEntityId, glm::vec2(0.0f));  // walked off it
      }
      path.push_back(center);
      if (!by) continue;
      m.hit.x = static_cast<int>(dir);
      m.hitX = by->entity;
      return;
    }
  }

  void walkY(BlockedMove& m, float dy) {
    if (dy == 0.0f) return;
    land(m, sweepY(center, dy), dy > 0.0f ? 1.0f : -1.0f);
    path.push_back(center);
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

}  // namespace

std::vector<EntityId> riders(World& world, EntityId platform) {
  std::vector<EntityId> found;
  const auto* pt = world.getComponent<TransformComponent>(platform);
  const auto* box = world.getComponent<BoxColliderComponent>(platform);
  const auto* terrain = world.getComponent<TerrainComponent>(platform);
  if (!pt || (!(box && box->blocksMask) && !terrain)) return found;
  for (auto [entity, t, c] : world.view<TransformComponent, BoxColliderComponent>()) {
    if (entity == platform || world.isPendingDestroy(entity) || world.parentOf(entity) != kNoEntityId) continue;
    if (c->blocksMask && !world.getComponent<VelocityComponent>(entity)) continue;
    const glm::vec2 center = glm::vec2(t->position) + c->offset;
    const float feet = center.y - c->halfExtents.y, left = center.x - c->halfExtents.x, right = center.x + c->halfExtents.x;
    auto standsOn = [&](float top) { return feet - top >= -kGap && feet - top <= kStanding; };
    bool on = false;
    if (box && (box->blocksMask & c->layerMask)) {
      const glm::vec2 at = glm::vec2(pt->position) + box->offset;
      on = std::abs(center.x - at.x) < c->halfExtents.x + box->halfExtents.x && standsOn(at.y + box->halfExtents.y);
    }
    if (!on && terrain) {
      forEachTerrainSegmentOf(world, platform, {left, feet - kStanding}, {right, feet + kGap}, c->layerMask,
                              [&](const TerrainSegment& seg) {
                                const auto s = span(Edge{seg.a, seg.b, platform, false, false}, 0, left, right);
                                on = on || (s && standsOn(s->hi));
                              });
    }
    if (on) found.push_back(entity);
  }
  return found;
}

namespace {

bool among(const std::vector<EntityId>& entities, EntityId e) { return std::find(entities.begin(), entities.end(), e) != entities.end(); }

struct Planned {
  BlockedMove m;
  glm::vec2 moved{0.0f};
  std::vector<glm::vec2> path{};  // its position at each turn, after the start
};

struct Style {
  float slide = 0.0f;
  bool dropThrough = false;
  bool walks = false;  // climbs slopes and steps, and stays on the ground going down
};
constexpr Style kCarried{};  // just shifted, as rigidly as it can be

// Where a collider's centers put its entity.
std::vector<glm::vec2> positions(std::vector<glm::vec2> centers, glm::vec2 offset) {
  for (glm::vec2& c : centers) c -= offset;
  return centers;
}

// How `mover` would move by `delta`, passing through `ignore`; changes nothing.
Planned plan(World& world, EntityId mover, glm::vec2 delta, Style style, const std::vector<EntityId>& ignore) {
  BlockedMove m;
  const auto* trans = world.getComponent<TransformComponent>(mover);
  const auto* collider = world.getComponent<BoxColliderComponent>(mover);
  if (!trans || !std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(style.slide)) return {};
  if (!collider) return {m, delta, {glm::vec2(trans->position) + delta}};  // nothing to block
  const glm::vec2 start = glm::vec2(trans->position) + collider->offset, half = collider->halfExtents;
  // Only what the move could reach: the box around its whole travel, slide and climbing included.
  const glm::vec2 pad(style.slide + 1.0f, style.slide + 1.0f + std::fabs(delta.x) * kClimb);
  const glm::vec2 reachMin = glm::min(start, start + delta) - half - pad, reachMax = glm::max(start, start + delta) + half + pad;
  std::vector<Box> solids;
  for (auto [entity, t, c] : world.view<TransformComponent, BoxColliderComponent>()) {
    if (entity == mover || !(c->blocksMask & collider->layerMask) || world.isPendingDestroy(entity) || among(ignore, entity))
      continue;
    const Box box{entity, glm::vec2(t->position) + c->offset, c->halfExtents};
    if (glm::any(glm::greaterThanEqual(box.center - box.half, reachMax)) ||
        glm::any(glm::lessThanEqual(box.center + box.half, reachMin)))
      continue;
    if (!overlaps(start, half, box)) solids.push_back(box);  // already inside one: free to leave it
  }
  std::vector<Edge> edges;
  forEachTerrainSegment(world, reachMin, reachMax, collider->layerMask, [&](const TerrainSegment& t) {
    if (t.entity == mover || t.a == t.b || among(ignore, t.entity)) return;
    if (!t.oneWay && overlapsSegment(Shape::box(start, half), t.a, t.b)) return;  // already in it: free to leave
    edges.push_back({t.a, t.b, t.entity, t.oneWay, std::fabs(t.b.y - t.a.y) <= kClimb * std::fabs(t.b.x - t.a.x)});
  });
  if (!style.walks && edges.empty()) {  // just boxes: exact, and can slide
    Mover body{start, half, solids};
    body.moveAxis(m, 0, delta.x, delta.y == 0.0f ? style.slide : 0.0f);
    body.moveAxis(m, 1, delta.y, delta.x == 0.0f ? style.slide : 0.0f);
    if (m.hit.y != 0) m.normal = glm::vec2(0.0f, -static_cast<float>(m.hit.y));
    return {m, body.center - start, positions(body.path, collider->offset)};
  }
  for (const Box& b : solids) {
    const glm::vec2 lo = b.center - b.half, hi = b.center + b.half, lr(hi.x, lo.y), ul(lo.x, hi.y);
    for (const auto& [a, z] : {std::pair(lo, lr), std::pair(lr, hi), std::pair(hi, ul), std::pair(ul, lo)})
      edges.push_back({a, z, b.entity, false, a.y == z.y});  // tops and bottoms are floors and ceilings
  }
  Walker body{start, glm::max(half, glm::vec2(kGap)), edges, style.dropThrough, style.walks};  // a point would slip between edges
  body.walkX(m, delta.x, delta.y);
  body.walkY(m, delta.y);
  return {m, body.center - start, positions(body.path, collider->offset)};
}

struct Member {
  EntityId entity;
  size_t carrier;  // the index of what it stands on in the group (the mover's own)
};

// The mover and everything riding on it, each once, carriers before what they carry.
std::vector<Member> groupOf(World& world, EntityId mover) {
  std::vector<Member> group{{mover, 0}};
  for (size_t i = 0; i < group.size(); ++i)
    for (const EntityId r : riders(world, group[i].entity))
      if (std::none_of(group.begin(), group.end(), [&](const Member& m) { return m.entity == r; })) group.push_back({r, i});
  return group;
}

// What group[j] stands on, and what that stands on, down to the mover.
std::vector<EntityId> carriersOf(const std::vector<Member>& group, size_t j) {
  std::vector<EntityId> carriers;
  for (size_t c = group[j].carrier; c != 0; c = group[c].carrier) carriers.push_back(group[c].entity);
  carriers.push_back(group[0].entity);
  return carriers;
}

// Whether group[k] stands, at some remove, on group[j].
bool carries(const std::vector<Member>& group, size_t j, size_t k) {
  for (size_t c = group[k].carrier; c != 0; c = group[c].carrier)
    if (c == j) return true;
  return false;
}

void shift(World& world, EntityId entity, glm::vec2 by) {
  if (auto* trans = world.getComponent<TransformComponent>(entity)) {
    trans->position.x += by.x;
    trans->position.y += by.y;
  }
}

// Carries the riders across by as much as each one's carrier went: front ones
// first, then again while any catch up, so none stays stopped where another was.
// Not those another platform carried across this `frame`: still on both, they'd go twice.
void carryAcross(World& world, const std::vector<Member>& group, const std::vector<EntityId>& all, float dx,
                 const MoveFrame* frame) {
  std::vector<float> went(group.size(), 0.0f);
  went[0] = dx;
  const auto ahead = [&](size_t j) {
    const EntityId e = group[j].entity;
    return (world.getComponent<TransformComponent>(e)->position.x + world.getComponent<BoxColliderComponent>(e)->offset.x) * dx;
  };
  // Held: taken across by another platform this frame, or standing on one that was.
  std::vector<bool> held(group.size(), false);
  for (size_t j = 1; frame && j < group.size(); ++j) {
    const auto it = frame->carrier.find(group[j].entity);
    held[j] = held[group[j].carrier] || (it != frame->carrier.end() && it->second != group[group[j].carrier].entity);
  }
  // What nothing outside stops goes all the way, so it's never in its carriers' way.
  std::vector<EntityId> clear;
  for (size_t j = 1; j < group.size(); ++j)
    if (!held[j] && plan(world, group[j].entity, {dx, 0.0f}, kCarried, all).moved.x == dx) clear.push_back(group[j].entity);
  std::vector<size_t> order(group.size() - 1);
  std::iota(order.begin(), order.end(), size_t{1});
  bool caughtUp = true;
  for (size_t pass = 0; caughtUp && pass <= group.size(); ++pass) {
    caughtUp = false;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return ahead(a) > ahead(b); });
    for (const size_t j : order) {
      const float behind = went[group[j].carrier] - went[j];
      if (behind == 0.0f || held[j]) continue;
      std::vector<EntityId> through = carriersOf(group, j);
      for (size_t k = j + 1; k < group.size(); ++k)
        if (carries(group, j, k) && among(clear, group[k].entity)) through.push_back(group[k].entity);
      const float step = plan(world, group[j].entity, {behind, 0.0f}, kCarried, through).moved.x;
      if (step == 0.0f) continue;
      went[j] += step;
      shift(world, group[j].entity, {step, 0.0f});
      caughtUp = true;
    }
  }
}

struct Carry {
  BlockedMove m;
  std::vector<glm::vec2> path;  // the mover's, as planned
  bool cut = false;             // its riders kept it from rising as far as planned
};

Carry carry(World& world, const std::vector<Member>& group, glm::vec2 delta, Style style, const MoveFrame* frame) {
  std::vector<EntityId> all;
  for (const Member& member : group) all.push_back(member.entity);
  const EntityId mover = group[0].entity;
  const Planned p = plan(world, mover, delta, style, all);
  Carry c{p.m, p.path};
  shift(world, mover, {p.moved.x, 0.0f});
  if (p.moved.x != 0.0f) carryAcross(world, group, all, p.moved.x, frame);
  float dy = p.moved.y;
  BlockedMove limit;
  for (size_t j = 1; j < group.size() && dy > 0.0f; ++j)
    if (const Planned r = plan(world, group[j].entity, {0.0f, dy}, kCarried, all); r.m.hit.y > 0 && r.moved.y < dy) {
      dy = std::max(r.moved.y, 0.0f);
      limit = r.m;
    }
  if (dy < p.moved.y) {  // a rider met something: so did it
    c.cut = true;
    c.m.hit.y = 1;
    c.m.hitY = limit.hitY;
    c.m.normal = limit.normal;
  }
  shift(world, mover, {0.0f, dy});
  if (dy > 0.0f) {  // checked above: all rise alike
    for (size_t j = 1; j < group.size(); ++j) shift(world, group[j].entity, {0.0f, dy});
    return c;
  }
  std::vector<float> fell(group.size(), dy);
  for (size_t j = 1; j < group.size(); ++j) {
    // Those already down are in its way (one may have stopped on a ledge); its carriers and the rest aren't.
    std::vector<EntityId> through = carriersOf(group, j);
    through.insert(through.end(), all.begin() + static_cast<std::ptrdiff_t>(j), all.end());
    const float follow = fell[group[j].carrier];
    fell[j] = follow == 0.0f ? 0.0f : plan(world, group[j].entity, {0.0f, follow}, kCarried, through).moved.y;
    shift(world, group[j].entity, {0.0f, fell[j]});
  }
  return c;
}

BlockedMove moveGroup(World& world, EntityId mover, glm::vec2 delta, Style style, MoveFrame* frame) {
  const std::vector<Member> group = groupOf(world, mover);
  std::vector<glm::vec3> start;
  for (const Member& member : group)
    if (const auto* trans = world.getComponent<TransformComponent>(member.entity)) start.push_back(trans->position);
  if (start.size() < group.size()) return {};  // no transform: nowhere to move
  if (group.size() > 1) style.slide = 0.0f;
  Carry c = carry(world, group, delta, style, frame);
  const bool walked = world.getComponent<TransformComponent>(mover)->position.x != start[0].x;
  if (c.cut && style.walks && walked) {  // a climb its riders can't make: none
    for (size_t j = 0; j < group.size(); ++j) world.getComponent<TransformComponent>(group[j].entity)->position = start[j];
    c = carry(world, group, delta, {.dropThrough = style.dropThrough}, frame);
  }
  if (!frame) return c.m;
  for (size_t j = 1; j < group.size(); ++j)  // carried across: theirs this frame
    if (world.getComponent<TransformComponent>(group[j].entity)->position.x != start[j].x)
      frame->carrier.try_emplace(group[j].entity, group[group[j].carrier].entity);
  // The mover went its planned way, unless its riders cut it short; the rest across, then up or down.
  for (size_t j = 0; j < group.size(); ++j) {
    const glm::vec2 from(start[j]), to(world.getComponent<TransformComponent>(group[j].entity)->position);
    std::vector<glm::vec2> via = j == 0 && !c.cut && !c.path.empty() ? c.path : std::vector<glm::vec2>{{to.x, from.y}, to};
    via.back() = to;  // as it ended, to the last float
    frame->went(group[j].entity, from, via);
  }
  return c.m;
}

}  // namespace

BlockedMove moveBlocked(World& world, EntityId mover, glm::vec2 delta, float slide, MoveFrame* frame) {
  return moveGroup(world, mover, delta, {.slide = slide}, frame);
}

BlockedMove walkBlocked(World& world, EntityId mover, glm::vec2 delta, bool dropThrough, MoveFrame* frame) {
  return moveGroup(world, mover, delta, {.dropThrough = dropThrough, .walks = true}, frame);
}

void MoveFrame::went(EntityId entity, glm::vec2 from, const std::vector<glm::vec2>& via) {
  std::vector<glm::vec2>& path = paths[entity];
  if (path.empty() || path.back() != from) path.assign(1, from);
  for (const glm::vec2 p : via)
    if (p != path.back()) path.push_back(p);
}
