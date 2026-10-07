#pragma once

#include <miniaudio.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "../core/async/LockFreeQueue.hpp"
#include "AudioHandle.hpp"
#include "SoundBuffer.hpp"
#include "VoiceCommand.hpp"
#include "VoiceManager.hpp"

// Audio front end. Changes are queued as VoiceCommands and applied by the audio
// thread, the only one that touches voices. play() reads the sound registry, so
// it must not run concurrently with registerSound() (both are main-thread or
// script calls today, and scripts run one at a time).
//
// The audio thread never frees a buffer: every buffer it can be holding is
// also held here, and one replaced by a re-registration is kept until no voice
// or queued command uses it, then freed on the main thread.
class AudioManager {
 public:
  AudioManager();
  ~AudioManager();

  // Registers a decoded sound under each of `names` (e.g. full asset path and
  // file name), replacing what they named; a null buffer (failed decode) stays
  // unknown. Main thread.
  void registerSound(std::initializer_list<std::string_view> names, std::shared_ptr<SoundBuffer> buffer);

  bool knows(std::string_view name) const { return _soundRegistry.contains(AudioHandle(name)); }

  // Returns 0 if the sound is unknown.
  SoundInstanceId play(AudioHandle handle, float gain = 1.0f, bool loop = false,
                       AudioBus bus = AudioBus::Sfx);
  void stop(SoundInstanceId instance);
  void fade(SoundInstanceId instance, float durationSeconds);
  void setGain(SoundInstanceId instance, float gain);

  void fadeOutAll(float durationSeconds);
  void stopAll();
  void setBusVolume(AudioBus bus, float volume);

 private:
  static void audioCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount);
  void send(VoiceCommand cmd);

  LockFreeQueue<VoiceCommand> _commands{4096};
  VoiceManager _voices;  // audio thread only
  ma_device _device;
  bool _deviceStarted = false;

  std::atomic<SoundInstanceId> _nextInstanceId{1};
  std::unordered_map<AudioHandle, std::shared_ptr<SoundBuffer>> _soundRegistry;
  // Replaced buffers a voice may still be playing; freed once only this holds them.
  std::vector<std::shared_ptr<SoundBuffer>> _retired;
};
