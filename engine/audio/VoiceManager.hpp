#pragma once

#include <algorithm>
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

  static constexpr size_t kMaxVoices = 128;
  // What's playing as of now, for the main thread (which can't look at voices).
  // A sound whose Play isn't applied yet counts as playing.
  struct Playing {
    std::array<SoundInstanceId, kMaxVoices> live{};  // 0: a free voice
    SoundInstanceId newestApplied = 0;               // newest Play applied, started or not
    bool contains(SoundInstanceId id) const {
      return id != 0 && (id > newestApplied || std::ranges::find(live, id) != live.end());
    }
  };
  Playing playing() const;

 private:
  std::array<Voice, kMaxVoices> _voices;
  std::array<float, static_cast<size_t>(AudioBus::Count)> _busGain;
  SoundInstanceId _newestApplied = 0;

  Voice* find(SoundInstanceId instance);
};
