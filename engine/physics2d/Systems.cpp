#include "Systems.hpp"

#include <algorithm>
#include <cmath>

#include "Blocking.hpp"
#include "BoxColliderComponent.hpp"
#include "Terrain.hpp"

float simulationStep(float dt) {
  constexpr float kMaxDt = 1.0f / 20.0f;
  return std::isfinite(dt) ? std::clamp(dt, 0.0f, kMaxDt) : 0.0f;
}

namespace {

// Whether a body goes by move/walk motion: it needs a box or terrain, and no parent.
bool movesBlocked(World& world, EntityId entity, const VelocityComponent& vel) {
  return (vel.motion == kMoveMotion || vel.motion == kWalkMotion) && world.parentOf(entity) == kNoEntityId &&
         (world.getComponent<BoxColliderComponent>(entity) || world.getComponent<GroundComponent>(entity));
}

// `bodies` reordered so each comes after what carries it, through whatever stands
// between (in a cycle, as listed).
std::vector<EntityId> carriersFirst(World& world, const std::vector<EntityId>& bodies) {
  std::vector<std::vector<EntityId>> carried(bodies.size());
  for (size_t i = 0; i < bodies.size(); ++i)
    for (std::vector<EntityId> next{bodies[i]}; !next.empty();) {
      const EntityId on = next.back();
      next.pop_back();
      for (const EntityId r : riders(world, on))
        if (r != bodies[i] && std::find(carried[i].begin(), carried[i].end(), r) == carried[i].end()) {
          carried[i].push_back(r);
          next.push_back(r);
        }
    }
  std::vector<EntityId> order;
  std::vector<bool> placed(bodies.size(), false);
  const auto place = [&](auto& self, size_t i) -> void {
    if (placed[i]) return;
    placed[i] = true;
    for (size_t c = 0; c < bodies.size(); ++c)
      if (std::find(carried[c].begin(), carried[c].end(), bodies[i]) != carried[c].end()) self(self, c);
    order.push_back(bodies[i]);
  };
  for (size_t i = 0; i < bodies.size(); ++i) place(place, i);
  return order;
}

}  // namespace

void MovementSystem::update(World& world, float dt) {
  dt = simulationStep(dt);
  _was.clear();
  _blocked.clear();
  _stopped.clear();
  for (auto [entity, trans, vel] : world.view<TransformComponent, VelocityComponent>()) {
    vel->velocity += vel->acceleration * dt;
    _was.emplace_back(entity, glm::vec2(trans->position));
    if (movesBlocked(world, entity, *vel)) {
      _blocked.push_back(entity);
      continue;
    }
    const glm::vec2 was(trans->position);
    trans->position.x += vel->velocity.x * dt;
    trans->position.y += vel->velocity.y * dt;
    vel->blocked = glm::vec2(0.0f);
    vel->floor = kNoEntityId;
    vel->platformVelocity = glm::vec2(0.0f);
    if (_frame) _frame->went(entity, was, {glm::vec2(trans->position)});  // a carry may add to it
  }
  // A carrier first: what it carries then moves on from where it was put.
  for (const EntityId entity : carriersFirst(world, _blocked)) {
    auto* vel = world.getComponent<VelocityComponent>(entity);
    const glm::vec2 step = vel->velocity * dt;
    const BlockedMove m = vel->motion == kWalkMotion ? walkBlocked(world, entity, step, vel->dropThrough != 0, _frame)
                                                    : moveBlocked(world, entity, step, 0.0f, _frame);
    vel->blocked = glm::vec2(m.hit);
    vel->floor = m.hit.y < 0 ? m.hitY : kNoEntityId;
    _stopped.emplace_back(entity, glm::vec2(world.getComponent<TransformComponent>(entity)->position));
    for (int axis = 0; axis < 2; ++axis)  // what stopped it stops its velocity that way
      if (m.hit[axis] != 0 && (vel->velocity[axis] > 0.0f) == (m.hit[axis] > 0)) vel->velocity[axis] = 0.0f;
  }
  for (const auto& [entity, was] : _was)  // carried along too
    world.getComponent<VelocityComponent>(entity)->travel = glm::vec2(world.getComponent<TransformComponent>(entity)->position) - was;
  for (const auto& [entity, stopped] : _stopped) {
    auto* vel = world.getComponent<VelocityComponent>(entity);
    // Pushed off where it stopped, or on something that moves (and may have left it): look again.
    const bool pushed = glm::vec2(world.getComponent<TransformComponent>(entity)->position) != stopped;
    if (pushed || (vel->floor != kNoEntityId && world.getComponent<VelocityComponent>(vel->floor)))
      vel->floor = floorOf(world, entity, vel->motion == kWalkMotion && vel->dropThrough != 0);
    const auto* under = vel->floor == kNoEntityId ? nullptr : world.getComponent<VelocityComponent>(vel->floor);
    vel->platformVelocity = under && dt > 0.0f ? under->travel / dt : glm::vec2(0.0f);
  }
}

void LifetimeSystem::update(World& world, float dt) {
  dt = simulationStep(dt);  // in step with movement: a bullet expires where it would have
  for (auto [entity, life] : world.view<LifetimeComponent>()) {
    life->seconds -= dt;
    if (life->seconds <= 0.0f) world.destroyDeferred(entity);
  }
}

void ScrollWrapSystem::update(World& world, float) {
  for (auto [entity, wrap, trans] : world.view<ScrollWrapComponent, TransformComponent>()) {
    const float span = wrap->maxY - wrap->minY;
    if (span <= 0.0f) continue;
    float& y = trans->position.y;
    if (y < wrap->minY) y += span * std::ceil((wrap->minY - y) / span);
    if (y > wrap->maxY) y -= span * std::ceil((y - wrap->maxY) / span);
  }
}

void CollisionSystem::update(World& world, float dt) {
  if (!std::isfinite(dt) || dt <= 0.0f) return;  // paused: nothing moved

  _proxies.clear();
  _ways.clear();
  _nextBodies.clear();
  _twoShaped.clear();
  forEachCollider(world, [&](const Collider& collider) { addProxy(world, collider); });
  std::swap(_bodies, _nextBodies);  // also forgets destroyed entities

  // Sort and sweep along x: each box meets only those whose x span (over this
  // frame's travel) it reaches, not every other box. The pairs are then put
  // back in world order, so scripts see the same collisions in the same order
  // as checking every pair.
  _byLeft.resize(_proxies.size());
  for (uint32_t i = 0; i < _proxies.size(); ++i) _byLeft[i] = i;
  std::stable_sort(_byLeft.begin(), _byLeft.end(), [&](uint32_t a, uint32_t b) { return _proxies[a].min.x < _proxies[b].min.x; });
  _active.clear();
  _pairs.clear();
  for (uint32_t index : _byLeft) {
    const Proxy& box = _proxies[index];
    std::erase_if(_active, [&](uint32_t other) { return _proxies[other].max.x <= box.min.x; });  // left behind
    for (uint32_t other : _active) {
      const Proxy& a = _proxies[std::min(index, other)];
      const Proxy& b = _proxies[std::max(index, other)];
      const Collider &ca = a.collider, &cb = b.collider;
      const bool interested = (ca.collisionLayer & cb.collisionMask) || (cb.collisionLayer & ca.collisionMask);
      if (a.max.y <= b.min.y || b.max.y <= a.min.y) continue;  // apart along y all along
      if (ca.entity != cb.entity && interested && (a.moves || b.moves) && touchedAlongWays(ca.shape, wayOf(a), cb.shape, wayOf(b)))
        _pairs.emplace_back(std::min(index, other), std::max(index, other));
    }
    _active.push_back(index);
  }
  std::sort(_pairs.begin(), _pairs.end());
  // An entity with a box and a circle touches another once, however many of their shapes meet.
  _reported.clear();
  auto twoShaped = [&](EntityId e) { return std::find(_twoShaped.begin(), _twoShaped.end(), e) != _twoShaped.end(); };
  for (auto [a, b] : _pairs) {
    const EntityId first = _proxies[a].collider.entity, second = _proxies[b].collider.entity;
    if (twoShaped(first) || twoShaped(second)) {
      const auto key = first < second ? std::pair(first, second) : std::pair(second, first);
      if (std::find(_reported.begin(), _reported.end(), key) != _reported.end()) continue;
      _reported.push_back(key);
    }
    _report(first, second);
  }
}

void CollisionSystem::addProxy(World& world, const Collider& collider) {
  const Shape& shape = collider.shape;
  const bool circle = shape.kind == Shape::Kind::Circle;
  const auto* velocity = world.getComponent<VelocityComponent>(collider.entity);
  // Its way this frame. Only moves and velocities sweep: a script that teleports something doesn't drag it across the screen.
  const uint32_t way = static_cast<uint32_t>(_ways.size());
  const glm::vec2 at(world.getComponent<TransformComponent>(collider.entity)->position);
  const std::vector<glm::vec2>* path = _moves ? _moves->pathOf(collider.entity) : nullptr;
  if (path && path->back() == at) {  // where it ended: not moved since
    for (const glm::vec2 p : *path) _ways.push_back(p - at);
  } else {
    if (velocity && velocity->travel != glm::vec2(0.0f)) _ways.push_back(-velocity->travel);
    _ways.push_back(glm::vec2(0.0f));
  }
  bool moves = velocity != nullptr || _ways.size() - way > 1;
  if (auto last = _bodies.find(collider.entity); last != _bodies.end()) {
    const auto& was = circle ? last->second.circle : last->second.box;  // a collider just added hasn't moved
    moves = moves || last->second.moves || (was && *was != shape.center);
  }
  Body& next = _nextBodies[collider.entity];
  if (next.box || next.circle) _twoShaped.push_back(collider.entity);
  (circle ? next.circle : next.box) = shape.center;
  next.moves = next.moves || moves;
  glm::vec2 lo(INFINITY), hi(-INFINITY);
  for (size_t i = way; i < _ways.size(); ++i) {
    lo = glm::min(lo, _ways[i]);
    hi = glm::max(hi, _ways[i]);
  }
  _proxies.push_back(Proxy{collider, way, static_cast<uint32_t>(_ways.size() - way), shape.center + lo - shape.extent(),
                           shape.center + hi + shape.extent(), moves});
}
