#pragma once

#include "../core/ecs/component/Component.hpp"

// Destroys its entity after `seconds` of (scaled) game time. Bullets, debris
// and explosions use this instead of a per-entity script.
struct LifetimeComponent : public Component<LifetimeComponent> {
  COMPONENT_NAME("LifetimeComponent");
  float seconds = 1.0f;
};
