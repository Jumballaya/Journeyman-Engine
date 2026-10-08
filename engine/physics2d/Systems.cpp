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

  _proxies.clear();
  _nextBodies.clear();
  for (auto [entity, trans, collider] : world.view<TransformComponent, BoxColliderComponent>()) {
    if (world.isPendingDestroy(entity)) continue;
    const glm::vec2 center = glm::vec2(trans->position) + collider->offset;
    auto last = _bodies.find(entity);
    const bool moves = world.hasComponent<VelocityComponent>(entity) ||
                       (last != _bodies.end() && (last->second.moves || last->second.center != center));
    _nextBodies[entity] = {center, moves};
    _proxies.push_back(Proxy{entity, center - collider->halfExtents, center + collider->halfExtents,
                             collider->layerMask, collider->collidesWithMask, moves});
  }
  std::swap(_bodies, _nextBodies);  // also forgets destroyed entities

  // Sort and sweep along x: each box meets only those whose x span it reaches,
  // not every other box. The pairs are then put back in world order, so
  // scripts see the same collisions in the same order as checking every pair.
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
      const bool overlap = a.max.x > b.min.x && a.min.x < b.max.x && a.max.y > b.min.y && a.min.y < b.max.y;
      if (interested && (a.moves || b.moves) && overlap) _pairs.emplace_back(std::min(index, other), std::max(index, other));
    }
    _active.push_back(index);
  }
  std::sort(_pairs.begin(), _pairs.end());
  for (auto [a, b] : _pairs) _report(_proxies[a].entity, _proxies[b].entity);
}
