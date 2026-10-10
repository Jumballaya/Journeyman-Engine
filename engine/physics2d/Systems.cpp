#include "Systems.hpp"

#include <algorithm>
#include <cmath>

float simulationStep(float dt) {
  constexpr float kMaxDt = 1.0f / 20.0f;
  return std::isfinite(dt) ? std::clamp(dt, 0.0f, kMaxDt) : 0.0f;
}

void MovementSystem::update(World& world, float dt) {
  dt = simulationStep(dt);
  for (auto [entity, trans, vel] : world.view<TransformComponent, VelocityComponent>()) {
    vel->velocity += vel->acceleration * dt;
    trans->position.x += vel->velocity.x * dt;
    trans->position.y += vel->velocity.y * dt;
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
  const float step = simulationStep(dt);  // how far velocities carried things this frame

  _proxies.clear();
  _nextBodies.clear();
  forEachCollider(world, [&](const Collider& collider) { addProxy(world, collider, step); });
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
  for (auto [a, b] : _pairs) {
    const EntityId first = _proxies[a].collider.entity, second = _proxies[b].collider.entity;
    if (_reported.insert(first < second ? std::pair(first, second) : std::pair(second, first)).second) _report(first, second);
  }
}

void CollisionSystem::addProxy(World& world, const Collider& collider, float step) {
  const Shape& shape = collider.shape;
  const int kind = static_cast<int>(shape.kind);
  auto last = _bodies.find(collider.entity);
  const auto* velocity = world.getComponent<VelocityComponent>(collider.entity);
  const bool moves = velocity || (last != _bodies.end() && (last->second.moves || last->second.center[kind] != shape.center));
  Body& next = _nextBodies[collider.entity];
  next.center[kind] = shape.center;
  next.moves = next.moves || moves;
  // Only velocity sweeps: a script that teleports something doesn't drag it across the screen.
  const glm::vec2 travel = velocity ? velocity->velocity * step : glm::vec2(0.0f);
  const glm::vec2 from = shape.center - travel;
  _proxies.push_back(
      Proxy{collider, travel, glm::min(from, shape.center) - shape.extent(), glm::max(from, shape.center) + shape.extent(), moves});
}
