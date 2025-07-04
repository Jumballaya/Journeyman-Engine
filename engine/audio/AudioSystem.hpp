#pragma once

#include <miniaudio.h>

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"
#include "AudioEmitterComponent.hpp"
#include "AudioManager.hpp"

class AudioSystem : public System {
 public:
  AudioSystem(AudioManager& audioManager) : _audioManager(audioManager) {}

  void update(World& world, float) override {
    _audioManager.update();

    for (auto [entity, emitter] : world.view<AudioEmitterComponent>()) {
      if (emitter->pendingSound.has_value()) {
        SoundInstanceId id = _audioManager.play(emitter->pendingSound.value(), emitter->gain, emitter->looping);
        emitter->activeSound = id;
        emitter->pendingSound.reset();
      }

      if (emitter->stopRequested && emitter->activeSound.has_value()) {
        _audioManager.stop(emitter->activeSound.value());
        emitter->activeSound.reset();
        emitter->stopRequested = false;
      }
    }
  }

  const char* name() const override { return "AudioSystem"; }

 private:
  AudioManager& _audioManager;
};