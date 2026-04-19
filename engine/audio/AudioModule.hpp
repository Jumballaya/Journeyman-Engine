#pragma once

#include <miniaudio.h>

#include "../core/app/Engine.hpp"
#include "../core/app/EngineModule.hpp"
#include "../core/assets/AssetRegistry.hpp"
#include "AudioHandle.hpp"
#include "AudioManager.hpp"

class AudioModule : public EngineModule {
 public:
  ~AudioModule() = default;

  void initialize(Engine& app) override;
  void shutdown(Engine& app) override;

  AudioManager& getAudioManager();

  const char* name() const override { return "AudioModule"; }

 private:
  AudioManager _audioManager;

  // Decoded sound handles keyed by the same AssetHandle the AssetManager
  // issued for the raw .wav/.ogg bytes. The converters populate this;
  // AudioEmitterComponent's JSON deserializer resolves name → loadAsset →
  // registry.get(handle).
  AssetRegistry<AudioHandle> _audio;
};
