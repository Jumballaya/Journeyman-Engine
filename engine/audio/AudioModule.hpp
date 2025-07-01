#pragma once

#include <miniaudio.h>

#include "../core/app/Application.hpp"
#include "../core/app/EngineModule.hpp"
#include "../core/assets/AssetHandle.hpp"
#include "AudioHandle.hpp"
#include "AudioManager.hpp"

class AudioModule : public EngineModule {
 public:
  void initialize(Application& app) override;
  void shutdown(Application& app) override;

  AudioManager& getAudioManager();

 private:
  AudioManager _audioManager;

  std::unordered_map<AssetHandle, AudioHandle> _handleMap;
};