#pragma once

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"
#include "../core/ecs/system/SystemTraits.hpp"
#include "AudioEmitterComponent.hpp"
#include "AudioManager.hpp"

// Starts emitters that haven't played yet.
class AudioSystem : public System {
 public:
  explicit AudioSystem(AudioManager& audio) : _audio(audio) {}

  void update(World& world, float) override {
    for (auto [entity, emitter] : world.view<AudioEmitterComponent>()) {
      if (emitter->playing == 0 && emitter->sound.isValid()) {
        emitter->playing = _audio.play(emitter->sound, emitter->gain, emitter->looping, emitter->bus);
      }
    }
  }

  const char* name() const override { return "AudioSystem"; }

 private:
  AudioManager& _audio;
};

template <>
struct SystemTraits<AudioSystem> {
  using DependsOn = EmptyList;
  using Provides = EmptyList;
  using Reads = EmptyList;
  using Writes = TypeList<AudioEmitterComponent>;
};
