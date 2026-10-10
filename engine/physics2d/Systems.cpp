#include "Systems.hpp"

#include <algorithm>
#include <cmath>

#include "Blocking.hpp"
#include "BoxColliderComponent.hpp"

float simulationStep(float dt) {
  constexpr float kMaxDt = 1.0f / 20.0f;
  return std::isfinite(dt) ? std::clamp(dt, 0.0f, kMaxDt) : 0.0f;
}

void MovementSystem::update(World& world, float dt) {
  dt = simulationStep(dt);
  for (auto [entity, trans, vel] : world.view<TransformComponent, VelocityComponent>()) {
    vel->velocity += vel->acceleration * dt;
    const glm::vec2 step = vel->velocity * dt, was(trans->position);
    const bool blocks = (vel->motion == kMoveMotion || vel->motion == kWalkMotion) &&
                        world.getComponent<BoxColliderComponent>(entity) && world.parentOf(entity) == kNoEntityId;
    if (!blocks) {
      trans->position.x += step.x;
      trans->position.y += step.y;
      vel->blocked = glm::vec2(0.0f);
      vel->travel = step;
      continue;
    }
    const BlockedMove m = vel->motion == kWalkMotion ? walkBlocked(world, entity, step, vel->dropThrough != 0)
                                                    : moveBlocked(world, entity, step);
    vel->blocked = glm::vec2(m.hit);
    vel->travel = glm::vec2(trans->position) - was;
    for (int axis = 0; axis < 2; ++axis)  // what stopped it stops its velocity that way
      if (m.hit[axis] != 0 && (vel->velocity[axis] > 0.0f) == (m.hit[axis] > 0)) vel->velocity[axis] = 0.0f;
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
