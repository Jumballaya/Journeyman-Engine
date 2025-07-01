#pragma once

#include <miniaudio.h>

#include "../core/ecs/World.hpp"
#include "../core/ecs/system/System.hpp"
#include "AudioEmitterComponent.hpp"
#include "AudioManager.hpp"

class AudioSystem : public System {
 public:
  AudioSystem(AudioManager& audioManager) : _audioManager(audioManager) {}

  void update(World& world, float dt) override {
    // for (auto [entity, emitter] : world.view<AudioEmitterComponent>()) {
    //   if (emitter->initialSound && !emitter->activeVoice) {
    //     VoiceId voiceId = _audioManager.play(emitter->initialSound.value(), emitter->gain, emitter->looping);
    //     emitter->activeVoice = voiceId;
    //   }

    //   // Handle new sound requests
    //   if (emitter->pendingSound) {
    //     VoiceId voiceId = _audioManager.play(emitter->pendingSound.value(), emitter->gain, emitter->looping);
    //     emitter->activeVoice = voiceId;
    //     emitter->pendingSound.reset();
    //   }

    //   // Handle stop request
    //   if (emitter->stopRequested && emitter->activeVoice) {
    //     _audioManager.stop(emitter->activeVoice.value());
    //     emitter->activeVoice.reset();
    //     emitter->stopRequested = false;
    //   }
    // }
    const uint32_t framesPerUpdate = static_cast<uint32_t>(dt * _audioManager.getSampleRate());
    _audioManager.update(framesPerUpdate);
  }

  const char* name() const override { return "AudioSystem"; }

 private:
  AudioManager& _audioManager;
};