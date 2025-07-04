#pragma once

#include <miniaudio.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "AudioHandle.hpp"
#include "SoundBuffer.hpp"
#include "SoundInstance.hpp"
#include "VoiceManager.hpp"

class AudioManager {
 public:
  AudioManager();
  ~AudioManager();

  void update();
  void mix(float* output, uint32_t frameCount, uint32_t channels);

  AudioHandle registerSound(std::string name, std::shared_ptr<SoundBuffer> buffer);
  SoundInstanceId play(AudioHandle handle, float gain = 1.0f, bool loop = false);

  void stop(SoundInstanceId instance);
  void fade(SoundInstanceId instance, float durationSeconds);
  void setGain(SoundInstanceId instance, float gain);

  void fadeOutAll(float durationSeconds);
  void setGainAll(float gain);
  void stopAll();

  uint32_t getSampleRate() const;

 private:
  static void audioCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount);

  VoiceManager _voiceManager;
  ma_device _device;
  uint32_t _sampleRate = 48000;
  uint32_t _channels = 2;

  SoundInstanceId _nextInstanceId = 1;

  std::unordered_map<AudioHandle, std::shared_ptr<SoundBuffer>> _soundRegistry;
  std::unordered_map<SoundInstanceId, SoundInstance> _activeInstances;

  // @TODO: VoiceId shouldn't be here and SoundInstanceId shouldn't be in the voice manager
  //        but I can't think of a better solution at the moment (pertains to AudioManager::update())
  std::unordered_map<VoiceId, SoundInstanceId> _voiceToInstance;
  std::vector<VoiceId> _finishedVoices;
  std::vector<std::pair<SoundInstanceId, VoiceId>> _startedVoices;
};