#include "SoundBuffer.hpp"

#include <miniaudio.h>

#include "../logger/logging.hpp"

namespace {

// Reads until the decoder runs dry: some formats can't report their length up front.
std::shared_ptr<SoundBuffer> drain(ma_decoder& decoder) {
  constexpr uint32_t kChannels = SoundBuffer::kChannels;
  constexpr ma_uint64 kChunkFrames = 16384;
  std::vector<float> samples;
  for (;;) {
    const size_t offset = samples.size();
    samples.resize(offset + kChunkFrames * kChannels);
    ma_uint64 read = 0;
    ma_decoder_read_pcm_frames(&decoder, samples.data() + offset, kChunkFrames, &read);
    samples.resize(offset + read * kChannels);
    if (read < kChunkFrames) break;
  }
  ma_decoder_uninit(&decoder);
  return SoundBuffer::fromSamples(std::move(samples), kChannels, SoundBuffer::kSampleRate);
}

}  // namespace

std::shared_ptr<SoundBuffer> SoundBuffer::decode(const std::vector<uint8_t>& bytes) {
  const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, kChannels, kSampleRate);
  ma_decoder decoder;
  if (ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder) != MA_SUCCESS) {
    JM_LOG_ERROR("[SoundBuffer] can't decode sound data");
    return nullptr;
  }
  return drain(decoder);
}

std::shared_ptr<SoundBuffer> SoundBuffer::fromFile(const std::filesystem::path& filePath) {
  const ma_decoder_config config = ma_decoder_config_init(ma_format_f32, kChannels, kSampleRate);
  ma_decoder decoder;
  if (ma_decoder_init_file(filePath.string().c_str(), &config, &decoder) != MA_SUCCESS) {
    JM_LOG_ERROR("[SoundBuffer] can't decode '{}'", filePath.string());
    return nullptr;
  }
  return drain(decoder);
}

std::shared_ptr<SoundBuffer> SoundBuffer::fromSamples(std::vector<float> samples, uint32_t channels,
                                                      uint32_t sampleRate) {
  auto buffer = std::make_shared<SoundBuffer>();
  buffer->_samples = std::move(samples);
  buffer->_channels = channels;
  buffer->_sampleRate = sampleRate;
  return buffer;
}
