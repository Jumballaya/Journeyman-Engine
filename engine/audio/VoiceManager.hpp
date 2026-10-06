#pragma once

#include <array>
#include <cstdint>

#include "VoiceCommand.hpp"

// The audio thread's mixer state. Not thread-safe by design: only the audio
// callback calls into it, after draining the command queue.
class VoiceManager {
 public:
  VoiceManager();

  void apply(const VoiceCommand& cmd);
  void mix(float* output, uint32_t frameCount, uint32_t channels);

  size_t activeVoiceCount() const;

 private:
  static constexpr size_t kMaxVoices = 128;
  std::array<Voice, kMaxVoices> _voices;
  std::array<float, static_cast<size_t>(AudioBus::Count)> _busGain;

  Voice* find(SoundInstanceId instance);
};
