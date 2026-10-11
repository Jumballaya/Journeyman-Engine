#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "../core/ecs/component/Component.hpp"
#include "../core/ecs/entity/EntityId.hpp"

// Moves the entity along points (from where it starts) at `speed`: a lift, a
// moving platform, a patrol. With a velocity it moves by it, carrying riders.
struct PathFollowerComponent : public Component<PathFollowerComponent> {
  COMPONENT_NAME("PathFollowerComponent");

  enum class Mode : uint32_t { PingPong, Loop, Once };  // Loop: the last point joins the first

  std::vector<glm::vec2> points;
  float speed = 60.0f;   // world units per second
  float wait = 0.0f;     // seconds stopped at each point
  bool smooth = false;   // slows into and out of each point
  Mode mode = Mode::PingPong;

  // Following: how far along it is (world units), which way, the wait left, and where it started.
  float distance = 0.0f;
  float direction = 1.0f;
  float waitLeft = 0.0f;
  glm::vec2 origin{0.0f};
  EntityId originParent = kNoEntityId;  // the space `origin` is in: under this parent, or the world
  bool started = false;
};
