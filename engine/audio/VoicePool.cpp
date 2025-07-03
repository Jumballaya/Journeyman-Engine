#include "VoicePool.hpp"

#include <iostream>

VoicePool::VoicePool(size_t capacity) {
  _voices.reserve(capacity);
  for (size_t i = 0; i < capacity; i++) {
    _voices.emplace_back();
  }
}

VoiceId VoicePool::acquireVoice(std::shared_ptr<SoundBuffer> buffer, float gain, bool looping) {
  Voice* voice = findFreeVoice();
  if (!voice) {
    std::cout << "[VoicePool] unable to find free voice\n";
    return 0;
  }

  VoiceId id = _nextVoiceId++;
  voice->initialize(id, std::move(buffer), gain, looping);
  _activeVoices.push_back(voice);
  return id;
}

Voice* VoicePool::getVoice(VoiceId id) {
  for (Voice& voice : _voices) {
    if (voice.id() == id && !voice.isFinished()) {
      return &voice;
    }
  }
  return nullptr;
}

const std::vector<Voice*>& VoicePool::activeVoices() const {
  return _activeVoices;
}

size_t VoicePool::activeVoiceCount() const {
  return _activeVoices.size();
}

void VoicePool::reset() {
  _activeVoices.clear();
  for (Voice& voice : _voices) {
    voice.reset();
  }
  _nextVoiceId = 1;
}

Voice* VoicePool::findFreeVoice() {
  for (Voice& voice : _voices) {
    if (voice.isFinished()) {
      return &voice;
    }
  }
  return nullptr;
}