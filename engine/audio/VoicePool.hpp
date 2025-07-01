#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <vector>

#include "Voice.hpp"

class VoicePool {
 public:
  VoicePool(size_t capacity);

  VoiceId acquireVoice(std::shared_ptr<SoundBuffer> buffer, float gain, bool looping);
  Voice* getVoice(VoiceId id);
  const std::vector<Voice*>& activeVoices() const;
  void reset();

  size_t activeVoiceCount() const;

 private:
  std::vector<Voice> _voices;
  std::vector<Voice*> _activeVoices;
  VoiceId _nextVoiceId = 1;

  Voice* findFreeVoice();
};
