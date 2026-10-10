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

// Where a body moved by move/walk motion starts (to order carriers before
// riders), or nothing if it moves freely: it needs a box or terrain, and no parent.
std::optional<float> blockedBottom(World& world, EntityId entity, const VelocityComponent& vel) {
  if ((vel.motion != kMoveMotion && vel.motion != kWalkMotion) || world.parentOf(entity) != kNoEntityId) return std::nullopt;
  float lowest = INFINITY;
  if (const auto* box = world.getComponent<BoxColliderComponent>(entity)) lowest = box->offset.y - box->halfExtents.y;
  if (const auto* terrain = world.getComponent<TerrainComponent>(entity))
    for (const TerrainChain& chain : terrain->chains) lowest = std::min(lowest, chain.min().y);
  if (lowest == INFINITY) return std::nullopt;
  return world.getComponent<TransformComponent>(entity)->position.y + lowest;
}

}  // namespace

void MovementSystem::update(World& world, float dt) {
  dt = simulationStep(dt);
  _was.clear();
  _blocked.clear();
  for (auto [entity, trans, vel] : world.view<TransformComponent, VelocityComponent>()) {
    vel->velocity += vel->acceleration * dt;
    _was.emplace_back(entity, glm::vec2(trans->position));
    if (const std::optional<float> bottom = blockedBottom(world, entity, *vel)) {
      _blocked.emplace_back(*bottom, entity);
      continue;
    }
    trans->position.x += vel->velocity.x * dt;
    trans->position.y += vel->velocity.y * dt;
    vel->blocked = glm::vec2(0.0f);
  }
  // Lowest first: a carrier is under what it carries, which then moves on from where it was put.
  std::stable_sort(_blocked.begin(), _blocked.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
  for (const auto& [bottom, entity] : _blocked) {
    auto* vel = world.getComponent<VelocityComponent>(entity);
    const glm::vec2 step = vel->velocity * dt;
    const BlockedMove m = vel->motion == kWalkMotion ? walkBlocked(world, entity, step, vel->dropThrough != 0)
                                                    : moveBlocked(world, entity, step);
    vel->blocked = glm::vec2(m.hit);
    for (int axis = 0; axis < 2; ++axis)  // what stopped it stops its velocity that way
      if (m.hit[axis] != 0 && (vel->velocity[axis] > 0.0f) == (m.hit[axis] > 0)) vel->velocity[axis] = 0.0f;
  }
  for (const auto& [entity, was] : _was)  // carried along too
    world.getComponent<VelocityComponent>(entity)->travel = glm::vec2(world.getComponent<TransformComponent>(entity)->position) - was;
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
      const bool interested = (ca.layerMask & cb.collidesWithMask) || (cb.layerMask & ca.collidesWithMask);
      if (ca.entity != cb.entity && interested && (a.moves || b.moves) && touchedDuring(ca.shape, a.travel, cb.shape, b.travel))
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
  bool moves = velocity != nullptr;
  if (auto last = _bodies.find(collider.entity); last != _bodies.end()) {
    const auto& was = circle ? last->second.circle : last->second.box;  // a collider just added hasn't moved
    moves = moves || last->second.moves || (was && *was != shape.center);
  }
  Body& next = _nextBodies[collider.entity];
  if (next.box || next.circle) _twoShaped.push_back(collider.entity);
  (circle ? next.circle : next.box) = shape.center;
  next.moves = next.moves || moves;
  // Only velocity sweeps: a script that teleports something doesn't drag it across the screen.
  const glm::vec2 travel = velocity ? velocity->travel : glm::vec2(0.0f);
  const glm::vec2 from = shape.center - travel;
  _proxies.push_back(
      Proxy{collider, travel, glm::min(from, shape.center) - shape.extent(), glm::max(from, shape.center) + shape.extent(), moves});
}
