#pragma once

#include "../core/ecs/component/Component.hpp"

struct AudioComponent : public Component<AudioComponent> {
  COMPONENT_NAME("AudioComponent");
  AudioComponent() {}
};