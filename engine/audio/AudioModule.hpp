#pragma once

#include "../core/app/EngineModule.hpp"
#include "AudioManager.hpp"

class Engine;

// Sound playback on the Master/Music/Sfx buses for scripts and AudioEmitterComponent.
// Sounds are named by path, file name ("shoot.wav") or stem ("shoot").
class AudioModule : public EngineModule {
 public:
  void initialize(Engine& app) override;
  void shutdown(Engine& app) override;
  const char* name() const override { return "AudioModule"; }

 private:
  AudioManager _audio;

  void bindScriptApi(Engine& app);
};
