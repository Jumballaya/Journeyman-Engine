#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "SoundBuffer.hpp"
#include "Voice.hpp"

class VoiceManager {
 public:
  void play(std::shared_ptr<SoundBuffer> buffer, float gain = 1.0f, bool looping = false);
  void mix(float* output, uint32_t frameCount, uint32_t channels);
  void fadeOutAll(uint32_t durationFrames);

 private:
  std::vector<std::unique_ptr<Voice>> _voices;
  std::mutex _mutex;
};