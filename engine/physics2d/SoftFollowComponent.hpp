#pragma once

#include <glm/glm.hpp>

#include "../core/ecs/component/Component.hpp"
#include "FollowTarget.hpp"

// Trails another entity at an offset, catching up smoothly: a pet, a trailing
// part, a marker over a unit (see SoftFollowSystem).
struct SoftFollowComponent : public Component<SoftFollowComponent> {
  COMPONENT_NAME("SoftFollowComponent");
  FollowTarget target;
  glm::vec2 offset{0.0f};  // from the target
  float smoothing = 6.0f;  // per second: higher catches up faster; 0 sticks to it
  bool mirror = false;     // offset x flips while the target faces left (scale x < 0): stays behind it
  bool started = false;    // found its target once (it starts there)
};
