#pragma once

#include "../core/ecs/component/Component.hpp"

// Endless scrolling: when the entity's y drops below `minY` it jumps up by
// (maxY - minY), and vice versa. Pair with a VelocityComponent to build
// looping parallax backgrounds out of a few tiles.
struct ScrollWrapComponent : public Component<ScrollWrapComponent> {
  COMPONENT_NAME("ScrollWrapComponent");
  float minY = -400.0f;
  float maxY = 400.0f;
};
