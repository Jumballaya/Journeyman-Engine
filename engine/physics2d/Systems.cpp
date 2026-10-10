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
  for (auto [entity, trans, box] : world.view<TransformComponent, BoxColliderComponent>()) {
    if (world.isPendingDestroy(entity)) continue;
    const glm::vec2 position(trans->position);
    addProxy(world, entity, position, Shape::box(position + box->offset, box->halfExtents), box->layerMask, box->collidesWithMask, step);
  }
  for (auto [entity, trans, circle] : world.view<TransformComponent, CircleColliderComponent>()) {
    if (world.isPendingDestroy(entity)) continue;
    const glm::vec2 position(trans->position);
    addProxy(world, entity, position, Shape::circle(position + circle->offset, circle->radius), circle->layerMask,
             circle->collidesWithMask, step);
  }
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
      const bool interested = (a.layerMask & b.collidesWithMask) || (b.layerMask & a.collidesWithMask);
      if (a.entity != b.entity && interested && (a.moves || b.moves) && touchedDuring(a.shape, a.travel, b.shape, b.travel))
        _pairs.emplace_back(std::min(index, other), std::max(index, other));
    }
    _active.push_back(index);
  }
  std::sort(_pairs.begin(), _pairs.end());
  for (auto [a, b] : _pairs) _report(_proxies[a].entity, _proxies[b].entity);
}

void CollisionSystem::addProxy(World& world, EntityId entity, glm::vec2 position, const Shape& shape, uint32_t layerMask,
                               uint32_t collidesWithMask, float step) {
  auto last = _bodies.find(entity);
  const auto* velocity = world.getComponent<VelocityComponent>(entity);
  const bool moves = velocity || (last != _bodies.end() && (last->second.moves || last->second.position != position));
  _nextBodies[entity] = {position, moves};
  // Only velocity sweeps: a script that teleports something doesn't drag it across the screen.
  const glm::vec2 travel = velocity ? velocity->velocity * step : glm::vec2(0.0f);
  const glm::vec2 from = shape.center - travel;
  _proxies.push_back(Proxy{entity, shape, travel, glm::min(from, shape.center) - shape.half, glm::max(from, shape.center) + shape.half,
                           layerMask, collidesWithMask, moves});
}
