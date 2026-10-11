#include "PathFollower.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

#include "Systems.hpp"

namespace {

using Mode = PathFollowerComponent::Mode;

// The points to go through (a loop's first again at the end) and each one's distance along.
struct Route {
  std::vector<glm::vec2> points;
  std::vector<float> at;
  float length() const { return at.empty() ? 0.0f : at.back(); }
};

Route routeOf(const PathFollowerComponent& c) {
  Route r;
  const auto add = [&r](glm::vec2 p) {
    if (!r.points.empty() && r.points.back() == p) return;  // no zero-length segments
    r.at.push_back(r.points.empty() ? 0.0f : r.at.back() + glm::length(p - r.points.back()));
    r.points.push_back(p);
  };
  for (glm::vec2 p : c.points) add(p);
  if (c.mode == Mode::Loop && r.points.size() > 1) add(r.points.front());
  return r;
}

// The point `d` along, eased within its segment when `smooth`.
glm::vec2 pointAt(const Route& r, float d, bool smooth) {
  if (r.points.empty()) return glm::vec2(0.0f);
  const size_t i = std::upper_bound(r.at.begin(), r.at.end(), d) - r.at.begin();
  if (i == 0) return r.points.front();
  if (i >= r.points.size()) return r.points.back();
  float t = (d - r.at[i - 1]) / (r.at[i] - r.at[i - 1]);
  if (smooth) t = t * t * (3.0f - 2.0f * t);
  return glm::mix(r.points[i - 1], r.points[i], t);
}

// The next point's distance along the way it goes, if there is one.
std::optional<float> nextPoint(const Route& r, float d, float direction) {
  constexpr float kPast = 1e-4f;
  if (direction > 0.0f) {
    const auto it = std::upper_bound(r.at.begin(), r.at.end(), d + kPast);
    return it == r.at.end() ? std::nullopt : std::optional(*it);
  }
  const auto it = std::lower_bound(r.at.begin(), r.at.end(), d - kPast);
  return it == r.at.begin() ? std::nullopt : std::optional(*(it - 1));
}

// `dt` seconds on: waiting at a point, else moving toward the next, and at an end turning, wrapping or stopping.
void advance(PathFollowerComponent& c, const Route& r, float dt) {
  if (r.length() <= 0.0f) return;
  c.distance = std::clamp(c.distance, 0.0f, r.length());
  float time = dt;
  for (int guard = 0; time > 0.0f && guard < 1000; ++guard) {
    if (c.waitLeft > 0.0f) {
      const float w = std::min(c.waitLeft, time);
      c.waitLeft -= w;
      time -= w;
      continue;
    }
    if (c.speed <= 0.0f) return;
    const std::optional<float> next = nextPoint(r, c.distance, c.direction);
    if (next) {
      const float toGo = std::abs(*next - c.distance);
      if (c.speed * time < toGo) {
        c.distance += c.direction * c.speed * time;
        return;
      }
      c.distance = *next;
      time -= toGo / c.speed;
    } else {
      c.distance = c.direction > 0.0f ? r.length() : 0.0f;  // within a hair of the end: there
    }
    const bool atEnd = c.direction > 0.0f ? c.distance >= r.length() : c.distance <= 0.0f;
    if (atEnd && c.mode == Mode::Once) return;
    if (atEnd && c.mode == Mode::PingPong) c.direction = -c.direction;
    if (atEnd && c.mode == Mode::Loop) c.distance = c.direction > 0.0f ? 0.0f : r.length();
    c.waitLeft = c.wait;
  }
}

}  // namespace

void PathFollowerSystem::update(World& world, float dt) {
  dt = simulationStep(dt);
  for (auto [entity, follower, transform] : world.view<PathFollowerComponent, TransformComponent>()) {
    auto* local = world.getComponent<LocalTransformComponent>(entity);  // a child goes along from its place under its parent
    if (!follower->started) {
      follower->origin = glm::vec2(local ? local->position : transform->position);
      follower->started = true;
    }
    const Route route = routeOf(*follower);
    advance(*follower, route, dt);
    const glm::vec2 to = follower->origin + pointAt(route, follower->distance, follower->smooth);
    if (local) {
      local->position.x = to.x;
      local->position.y = to.y;
    } else if (auto* velocity = world.getComponent<VelocityComponent>(entity)) {
      velocity->velocity = dt > 0.0f ? (to - glm::vec2(transform->position)) / dt : glm::vec2(0.0f);
    } else {
      transform->position.x = to.x;
      transform->position.y = to.y;
    }
  }
}
