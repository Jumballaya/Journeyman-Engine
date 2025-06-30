#pragma once

#include <miniaudio.h>

#include <memory>
#include <string>

#include "SoundBuffer.hpp"
#include "VoiceManager.hpp"

class AudioManager {
 public:
  AudioManager();
  ~AudioManager();

  void update();
  void play(std::shared_ptr<SoundBuffer> buffer, float gain = 1.0f, bool loop = false);
  void fadeOutAll(float durationSeconds);

 private:
  static void audioCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount);

  std::unique_ptr<VoiceManager> _voiceManager;
  ma_device _device;
  uint32_t _sampleRate;
  uint32_t _channels;
};