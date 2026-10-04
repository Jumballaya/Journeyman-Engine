#pragma once

#include "../core/app/EngineModule.hpp"
#include "AudioManager.hpp"

class Engine;

// Sound playback for the game: decodes .wav/.ogg/.mp3/.flac assets, plays
// them on the Master/Music/Sfx buses, and exposes Sound/Audio to scripts and
// AudioEmitterComponent to scenes. Sounds are named by asset path, file name
// ("shoot.wav") or stem ("shoot").
class AudioModule : public EngineModule {
 public:
  void initialize(Engine& app) override;
  void shutdown(Engine& app) override;
  const char* name() const override { return "AudioModule"; }

 private:
  AudioManager _audio;

  void bindScriptApi(Engine& app);
};
