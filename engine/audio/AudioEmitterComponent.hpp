#pragma once

#include <optional>

#include "../core/ecs/component/Component.hpp"
#include "AudioHandle.hpp"
#include "SoundInstance.hpp"

struct AudioEmitterComponent : public Component<AudioEmitterComponent> {
  COMPONENT_NAME("AudioEmitterComponent");
  AudioEmitterComponent() {}

  std::optional<AudioHandle> initialSound;
  std::optional<SoundInstanceId> activeSound;
  float gain = 1.0f;
  bool looping = false;

  std::optional<AudioHandle> pendingSound;
  bool stopRequested = false;
};