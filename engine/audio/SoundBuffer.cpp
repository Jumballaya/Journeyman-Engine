#include "SoundBuffer.hpp"

#include <miniaudio.h>

#include <iostream>
#include <stdexcept>

std::shared_ptr<SoundBuffer> SoundBuffer::decode(const std::vector<uint8_t>& binary) {
  auto buffer = std::make_shared<SoundBuffer>();

  ma_result result;
  ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, 48000);

  ma_decoder decoder;
  result = ma_decoder_init_memory(binary.data(), binary.size(), &config, &decoder);
  if (result != MA_SUCCESS) {
    throw std::runtime_error("Failed to load sound from memory");
  }

  buffer->_sampleRate = decoder.outputSampleRate;
  buffer->_numChannels = decoder.outputChannels;

  ma_uint64 frameCount = 0;
  result = ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);
  if (result != MA_SUCCESS) {
    ma_decoder_uninit(&decoder);
    throw std::runtime_error("Failed to get length of sound from memory");
  }

  buffer->_totalFrames = frameCount;
  buffer->_samples.resize(buffer->_totalFrames * buffer->_numChannels);

  ma_decoder_read_pcm_frames(&decoder, buffer->_samples.data(), buffer->_totalFrames, nullptr);
  ma_decoder_uninit(&decoder);

  return buffer;
}

std::shared_ptr<SoundBuffer> SoundBuffer::fromFile(const std::filesystem::path& filePath) {
  auto buffer = std::make_shared<SoundBuffer>();

  ma_result result;
  ma_decoder decoder;
  ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 2, 48000);
  result = ma_decoder_init_file(filePath.string().c_str(), &config, &decoder);
  if (result != MA_SUCCESS) {
    throw std::runtime_error("Failed to load from file");
  }

  buffer->_sampleRate = decoder.outputSampleRate;
  buffer->_numChannels = decoder.outputChannels;

  ma_uint64 frameCount = 0;
  result = ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);
  if (result != MA_SUCCESS) {
    ma_decoder_uninit(&decoder);
    throw std::runtime_error("Failed to get length file");
  }

  buffer->_totalFrames = frameCount;
  buffer->_samples.resize(buffer->_totalFrames * buffer->_numChannels);

  ma_decoder_read_pcm_frames(&decoder, buffer->_samples.data(), buffer->_totalFrames, nullptr);
  ma_decoder_uninit(&decoder);

  std::cout << "First 10 items in buffer:\n";
  for (int i = 0; i < 10; ++i) {
    std::cout << buffer->data()[i];
  }
  std::cout << "\n";

  return buffer;
}

const float* SoundBuffer::data() const {
  return _samples.data();
}

size_t SoundBuffer::totalFrames() const {
  return _totalFrames;
}

uint32_t SoundBuffer::channels() const {
  return _numChannels;
}

uint32_t SoundBuffer::sampleRate() const {
  return _sampleRate;
}

float SoundBuffer::getDuration() const {
  return static_cast<float>(_totalFrames) / static_cast<float>(_sampleRate);
}