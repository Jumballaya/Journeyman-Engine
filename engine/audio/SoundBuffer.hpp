#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

// A fully decoded sound: interleaved float samples.
class SoundBuffer {
 public:
  // The output device's format; decoded sounds are converted to it.
  static constexpr uint32_t kChannels = 2;
  static constexpr uint32_t kSampleRate = 48000;

  // Decode any format miniaudio sniffs; null (logged) if it doesn't decode.
  static std::shared_ptr<SoundBuffer> decode(const std::vector<uint8_t>& bytes);
  static std::shared_ptr<SoundBuffer> fromFile(const std::filesystem::path& filePath);
  // Interleaved float samples already at the device rate (tests, synthesis).
  static std::shared_ptr<SoundBuffer> fromSamples(std::vector<float> samples, uint32_t channels,
                                                  uint32_t sampleRate);

  const float* data() const { return _samples.data(); }
  size_t totalFrames() const { return _channels ? _samples.size() / _channels : 0; }
  uint32_t channels() const { return _channels; }
  float getDuration() const { return _sampleRate ? static_cast<float>(totalFrames()) / _sampleRate : 0.0f; }

 private:
  std::vector<float> _samples;
  uint32_t _sampleRate = 0;
  uint32_t _channels = 0;
};
