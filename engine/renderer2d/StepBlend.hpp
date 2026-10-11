#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "../core/ecs/World.hpp"
#include "../physics2d/TransformComponent.hpp"

// What a drawn frame shows between two simulation steps: every transform and
// the camera moved `fraction` of the way from where the step before the last
// left them to where they are now. A move longer than half the visible view in
// one step (a teleport, a wrap, a new room) is drawn where it lands.
class StepBlend {
 public:
  struct Camera {
    glm::vec2 center{0.0f};
    float zoom = 1.0f;
  };

  // After each step: where things are now.
  void stepped(World& world, Camera camera) {
    std::swap(_was, _is);
    _is.clear();
    for (auto [entity, transform] : world.view<TransformComponent>()) _is.emplace(entity, *transform);
    _cameraWas = std::exchange(_cameraIs, camera);
    if (std::exchange(_fresh, false)) {  // a new scene: nothing to come from
      _was.clear();
      _cameraWas = camera;
    }
  }
  // A new scene started: its first step is drawn as it is.
  void forget() { _fresh = true; }

  // Places the transforms between steps until restore(); the camera to draw with.
  // `viewSize`: the view's smaller side in logical pixels; a jump is half of it at
  // the step's closer zoom, so a zoom out doesn't blend a teleport across the screen.
  Camera blend(World& world, Camera camera, float fraction, float viewSize) {
    _live.clear();
    if (_fresh || fraction >= 1.0f) return camera;  // a new scene before its first step: nothing to come from
    const float maxJump = 0.5f * viewSize / std::max(_cameraWas.zoom, camera.zoom);
    const auto near = [&](glm::vec2 a, glm::vec2 b) { return glm::length(b - a) <= maxJump; };
    for (auto [entity, transform] : world.view<TransformComponent>()) {
      const auto was = _was.find(entity);
      if (was == _was.end() || !near(glm::vec2(was->second.position), glm::vec2(transform->position))) continue;
      _live.emplace_back(entity, *transform);
      const TransformComponent& from = was->second;
      transform->position = glm::mix(from.position, transform->position, fraction);
      transform->scale = glm::mix(from.scale, transform->scale, fraction);
      transform->rotationRad = from.rotationRad + shortestTurn(from.rotationRad, transform->rotationRad) * fraction;
    }
    if (!near(_cameraWas.center, camera.center)) return camera;
    return {glm::mix(_cameraWas.center, camera.center, fraction), glm::mix(_cameraWas.zoom, camera.zoom, fraction)};
  }
  void restore(World& world) {
    for (const auto& [entity, live] : _live) {
      if (auto* transform = world.getComponent<TransformComponent>(entity)) *transform = live;
    }
    _live.clear();
  }

 private:
  std::unordered_map<EntityId, TransformComponent> _was, _is;
  std::vector<std::pair<EntityId, TransformComponent>> _live;  // blended ones' real places, while drawing
  Camera _cameraWas, _cameraIs;
  bool _fresh = true;

  static float shortestTurn(float from, float to) {
    constexpr float kTau = 2.0f * std::numbers::pi_v<float>;
    return std::remainder(to - from, kTau);
  }
};
