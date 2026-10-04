#pragma once

#include "../core/ecs/component/Component.hpp"
#include "AudioHandle.hpp"
#include "Voice.hpp"

// Plays `sound` once the entity exists; stops (fading) when it is destroyed.
// Scene music is just an entity with a looping emitter on the Music bus.
struct AudioEmitterComponent : public Component<AudioEmitterComponent> {
  COMPONENT_NAME("AudioEmitterComponent");
  AudioHandle sound;
  float gain = 1.0f;
  bool looping = false;
  AudioBus bus = AudioBus::Sfx;
  SoundInstanceId playing = 0;  // 0 until started
};
