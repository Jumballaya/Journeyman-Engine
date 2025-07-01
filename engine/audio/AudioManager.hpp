#pragma once

#include <miniaudio.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "AudioHandle.hpp"
#include "SoundBuffer.hpp"
#include "VoiceManager.hpp"

class AudioManager {
 public:
  AudioManager();
  ~AudioManager();

  void update(uint32_t framesPerUpdate);
  void mix(float* output, uint32_t frameCount, uint32_t channels);

  AudioHandle registerSound(std::string name, std::shared_ptr<SoundBuffer> buffer);
  void play(AudioHandle handle, float gain = 1.0f, bool loop = false);
  void fadeOutAll(float durationSeconds);
  void setGainAll(float gain);
  void stopAll();

 private:
  static void audioCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount);

  VoiceManager _voiceManager;
  ma_device _device;
  uint32_t _sampleRate = 48000;
  uint32_t _channels = 2;

  std::unordered_map<AudioHandle, std::shared_ptr<SoundBuffer>> _soundRegistry;
};