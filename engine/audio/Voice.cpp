#include "Voice.hpp"

void Voice::start(SoundInstanceId instance, std::shared_ptr<SoundBuffer> buffer, float gain,
                  bool looping, AudioBus bus) {
  _instance = instance;
  _buffer = std::move(buffer);
  _cursor = 0;
  _gain = gain;
  _looping = looping;
  _bus = bus;
  _fadeGain = 1.0f;
  _fadeStep = 0.0f;
}

void Voice::fadeOut(uint32_t frames) {
  if (frames == 0) {
    stop();
    return;
  }
  _fadeStep = _fadeGain / static_cast<float>(frames);
}

void Voice::mix(float* out, uint32_t frameCount, uint32_t channels, float busGain) {
  if (!_buffer) return;
  const float* data = _buffer->data();
  const uint32_t srcChannels = _buffer->channels();
  const size_t totalFrames = _buffer->totalFrames();
  if (totalFrames == 0 || srcChannels == 0) {
    stop();
    return;
  }

  for (uint32_t i = 0; i < frameCount; ++i) {
    if (_cursor >= totalFrames) {
      if (!_looping) {
        stop();
        return;
      }
      _cursor = 0;
    }
    if (_fadeStep > 0.0f) {
      _fadeGain -= _fadeStep;
      if (_fadeGain <= 0.0f) {
        stop();
        return;
      }
    }
    const float g = _gain * _fadeGain * busGain;
    const float* frame = data + _cursor * srcChannels;
    for (uint32_t ch = 0; ch < channels; ++ch) {
      out[i * channels + ch] += g * frame[ch % srcChannels];
    }
    ++_cursor;
  }
}
