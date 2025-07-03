#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../core/assets/RawAsset.hpp"

class SoundBuffer {
 public:
  static std::shared_ptr<SoundBuffer> decode(const std::vector<uint8_t>& bytes);
  static std::shared_ptr<SoundBuffer> fromFile(const std::filesystem::path& filePath);

  SoundBuffer() = default;

  const float* data() const;
  size_t totalFrames() const;
  uint32_t channels() const;
  uint32_t sampleRate() const;
  float getDuration() const;

 private:
  std::vector<float> _samples;
  uint32_t _sampleRate = 0;
  uint32_t _numChannels = 0;
  size_t _totalFrames = 0;
};