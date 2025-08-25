#pragma once

#include <miniaudio.h>

#include "../core/app/Application.hpp"
#include "../core/app/EngineModule.hpp"
#include "../core/assets/AssetHandle.hpp"
#include "AudioHandle.hpp"
#include "AudioManager.hpp"

class AudioModule : public EngineModule {
 public:
  ~AudioModule() = default;

  void initialize(Application& app) override;
  void shutdown(Application& app) override;

  AudioManager& getAudioManager();

  const char* name() const override { return "AudioModule"; }

 private:
  AudioManager _audioManager;

  std::unordered_map<AssetHandle, AudioHandle> _handleMap;
};