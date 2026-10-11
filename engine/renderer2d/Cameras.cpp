#include "Cameras.hpp"

#include <algorithm>
#include <cmath>

#include "../physics2d/TransformComponent.hpp"
#include "../physics2d/VelocityComponent.hpp"

namespace {

float zoomOf(float zoom) { return std::isfinite(zoom) && zoom > 0.0f ? zoom : 1.0f; }

// A center keeping a view `half` wide inside [lo, hi] (centered if it's wider).
float inside(float center, float lo, float hi, float half) {
  return hi - lo <= half * 2.0f ? (lo + hi) * 0.5f : std::clamp(center, lo + half, hi - half);
}

// The entity followed: the one set, else the first named `target` (a respawned player).
EntityId targetOf(World& world, CameraComponent& camera) {
  if (world.isAlive(camera.targetId) || camera.target.empty()) return camera.targetId;
  const auto named = world.findWithTag(camera.target);
  camera.targetId = named.empty() ? kNoEntityId : *std::min_element(named.begin(), named.end(), [](EntityId a, EntityId b) {
    return a.index < b.index;
  });
  return camera.targetId;
}

// Moves `center` after the target (dead zone, look-ahead), keeping the view inside
// the target's region or the bounds; it pans over when that changes.
void followTarget(World& world, CameraComponent& camera, glm::vec2& center, float dt, glm::vec2 gameSize) {
  const EntityId target = targetOf(world, camera);
  const auto* at = world.getComponent<TransformComponent>(target);
  if (!at) return;
  const glm::vec2 to(at->position);
  const auto* velocity = world.getComponent<VelocityComponent>(target);
  const float vx = velocity ? velocity->velocity.x : 0.0f;
  const float k = camera.smoothing > 0.0f ? 1.0f - std::exp(-camera.smoothing * std::max(0.0f, dt)) : 1.0f;
  if (!camera.started) {  // on the target at once
    camera.base = to;
    camera.ahead = 0.0f;
  }
  // Each eases on its own, so the sum doesn't depend on how frames fall.
  const float wantAhead = vx > 0.0f ? camera.lookAhead : vx < 0.0f ? -camera.lookAhead : camera.ahead;
  camera.ahead += (wantAhead - camera.ahead) * k;
  const glm::vec2 half = camera.deadZone * 0.5f;
  camera.base += (glm::clamp(camera.base, to - half, to + half) - camera.base) * k;

  int region = -1;
  for (int i = 0; i < static_cast<int>(camera.regions.size()) && region < 0; ++i) {
    if (camera.regions[i].contains(to)) region = i;
  }
  glm::vec2 goal = camera.base + glm::vec2(camera.ahead, 0.0f);
  const CameraComponent::Rect* box = region >= 0 ? &camera.regions[region] : camera.bounds ? &*camera.bounds : nullptr;
  if (box) {
    const glm::vec2 view = gameSize * 0.5f / zoomOf(camera.zoom);
    goal = {inside(goal.x, box->min.x, box->max.x, view.x), inside(goal.y, box->min.y, box->max.y, view.y)};
  }
  camera.panning = camera.started && (camera.panning || region != camera.region);
  camera.region = region;
  camera.started = true;
  if (!camera.panning) {
    center = goal;
    return;
  }
  center += (goal - center) * k;
  camera.panning = glm::length(goal - center) > 0.5f;  // there: follows exactly again
}

}  // namespace

EntityId Cameras::pick(World& world) const {
  EntityId best = kNoEntityId;
  float bestPriority = 0.0f;
  for (auto [entity, camera, transform] : world.view<CameraComponent, TransformComponent>()) {
    // The one shown keeps it on a tie, so equal cameras don't trade places.
    if (best == kNoEntityId || camera->priority > bestPriority || (camera->priority == bestPriority && entity == _shown)) {
      best = entity;
      bestPriority = camera->priority;
    }
  }
  return best;
}

void Cameras::update(World& world, float dt, glm::vec2 gameSize) {
  for (auto [entity, camera, transform] : world.view<CameraComponent, TransformComponent>()) {
    glm::vec2 center(transform->position);
    followTarget(world, *camera, center, dt, gameSize);
    transform->position.x = center.x;
    transform->position.y = center.y;
  }
  const EntityId best = pick(world);
  const auto* camera = world.getComponent<CameraComponent>(best);
  const auto* transform = world.getComponent<TransformComponent>(best);
  const View target = camera && transform ? View{glm::vec2(transform->position), zoomOf(camera->zoom)} : _free;
  if (best != _shown) {
    const auto* next = world.getComponent<CameraComponent>(best);
    _blendDuration = next && _shown != kNoEntityId ? std::max(0.0f, next->blend) : 0.0f;
    _blendTime = 0.0f;
    _from = _view;
    _shown = best;
  }
  _blendTime += dt;
  if (_blendTime >= _blendDuration) {
    _view = target;
    return;
  }
  const float t = _blendTime / _blendDuration;
  const float s = t * t * (3.0f - 2.0f * t);  // smoothstep: eases out and in
  _view = {glm::mix(_from.center, target.center, s), glm::mix(_from.zoom, target.zoom, s)};
}

void Cameras::setPosition(World& world, glm::vec2 center) {
  const EntityId shown = world.isAlive(_shown) ? _shown : pick(world);  // a script's first frame: before any update
  auto* camera = world.getComponent<CameraComponent>(shown);
  auto* transform = world.getComponent<TransformComponent>(shown);
  if (!camera || !transform) {
    _free.center = _view.center = center;
    return;
  }
  transform->position.x = center.x;
  transform->position.y = center.y;
  camera->started = true;
  camera->base = center;
  camera->panning = false;
  camera->ahead = 0.0f;  // a jump (a respawn): no easing, no look-ahead carried over
  _view.center = center;
  _blendDuration = 0.0f;
}

void Cameras::setZoom(World& world, float zoom) {
  zoom = zoomOf(zoom);
  if (auto* camera = world.getComponent<CameraComponent>(world.isAlive(_shown) ? _shown : pick(world))) camera->zoom = zoom;
  else _free.zoom = zoom;
  _view.zoom = zoom;
  _blendDuration = 0.0f;
}

void Cameras::follow(World& world, EntityId camera, EntityId target) {
  auto* c = world.getComponent<CameraComponent>(camera);
  if (!c) return;
  c->target.clear();  // the script's choice: no falling back to the authored name
  c->targetId = target;
}
