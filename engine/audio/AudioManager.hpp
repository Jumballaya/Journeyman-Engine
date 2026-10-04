#pragma once

#include <miniaudio.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "../core/async/LockFreeQueue.hpp"
#include "AudioHandle.hpp"
#include "SoundBuffer.hpp"
#include "VoiceCommand.hpp"
#include "VoiceManager.hpp"

// Front end of the audio engine. Callable from the main thread and from
// script worker threads; every playback change is queued as a VoiceCommand
// that the audio callback applies before mixing, so voices are only ever
// touched by the audio thread.
class AudioManager {
 public:
  AudioManager();
  ~AudioManager();

  // Registers a decoded sound under each of `names` (e.g. full asset path and
  // file name). Main thread, during asset loading.
  void registerSound(std::initializer_list<std::string_view> names, std::shared_ptr<SoundBuffer> buffer);

  // Returns 0 if the sound is unknown.
  SoundInstanceId play(AudioHandle handle, float gain = 1.0f, bool loop = false,
                       AudioBus bus = AudioBus::Sfx);
  void stop(SoundInstanceId instance);
  void fade(SoundInstanceId instance, float durationSeconds);
  void setGain(SoundInstanceId instance, float gain);

  void fadeOutAll(float durationSeconds);
  void stopAll();
  void setBusVolume(AudioBus bus, float volume);

  uint32_t getSampleRate() const { return _sampleRate; }

 private:
  static void audioCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount);
  void send(VoiceCommand cmd);
  uint32_t framesFor(float seconds) const;

  LockFreeQueue<VoiceCommand> _commands{4096};
  VoiceManager _voices;  // audio thread only
  ma_device _device;
  bool _deviceStarted = false;
  uint32_t _sampleRate = 48000;
  uint32_t _channels = 2;

  std::atomic<SoundInstanceId> _nextInstanceId{1};
  std::unordered_map<AudioHandle, std::shared_ptr<SoundBuffer>> _soundRegistry;
};
