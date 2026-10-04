#pragma once

#include "../core/ecs/component/Component.hpp"

// Wraps y into [minY, maxY): with a velocity, a few tiles make an endless
// scrolling background.
struct ScrollWrapComponent : public Component<ScrollWrapComponent> {
  COMPONENT_NAME("ScrollWrapComponent");
  float minY = -400.0f;
  float maxY = 400.0f;
};
