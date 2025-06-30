#include "Voice.hpp"

#include <cstring>

Voice::Voice(std::shared_ptr<SoundBuffer> buffer) : _buffer(std::move(buffer)) {}

bool Voice::isFinished() const {
  return _state == State::Finished;
}

void Voice::mix(float* out, uint32_t frameCount) {
  if (isFinished()) return;

  const float* data = _buffer->data();
  uint32_t channels = _buffer->channels();
  size_t totalFrames = _buffer->totalFrames();

  for (uint32_t i = 0; i < frameCount; ++i) {
    if (_cursor >= totalFrames) {
      if (_looping) {
        _cursor = 0;
      } else {
        _state = State::Finished;
        break;
      }
    }

    // Apply the Fade-out
    float gain = _gain;
    if (_state == State::FadingOut) {
      gain *= _currentGain;
      if (_fadeRemaining == 0) {
        _state = State::Finished;
        break;
      }

      // linear fade
      _currentGain *= (_fadeRemaining - 1) / static_cast<float>(_fadeRemaining);
      _fadeRemaining--;
    }

    for (uint32_t ch = 0; ch < channels; ++ch) {
      size_t sampleIndex = _cursor * channels + ch;
      out[i * channels + ch] += gain * data[sampleIndex];
    }
    _cursor++;
  }
}

void Voice::fadeOut(uint32_t durationFrames) {
  if (_state == State::Playing) {
    _state = State::FadingOut;
    _fadeRemaining = durationFrames;
  }
}