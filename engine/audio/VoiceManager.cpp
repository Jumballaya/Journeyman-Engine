#include "VoiceManager.hpp"

#include <algorithm>
#include <cstring>

void VoiceManager::play(std::shared_ptr<SoundBuffer> buffer, float gain, bool looping) {
  std::lock_guard lock(_mutex);
  auto voice = std::make_unique<Voice>(std::move(buffer));
  voice->setGain(gain);
  voice->setLooping(looping);
  _voices.emplace_back(std::move(voice));
}

void VoiceManager::mix(float* output, uint32_t frameCount, uint32_t channels) {
  std::lock_guard lock(_mutex);
  std::memset(output, 0, sizeof(float) * frameCount * channels);

  for (auto& voice : _voices) {
    if (!voice->isFinished()) {
      voice->mix(output, frameCount);
    }
  }

  _voices.erase(std::remove_if(
                    _voices.begin(),
                    _voices.end(),
                    [](const std::unique_ptr<Voice>& v) {
                      return v->isFinished();
                    }),
                _voices.end());
}

void VoiceManager::fadeOutAll(uint32_t durationFrames) {
  std::lock_guard lock(_mutex);
  for (auto& v : _voices) {
    if (!v->isFinished()) {
      v->fadeOut(durationFrames);
    }
  }
}