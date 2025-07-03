#include "Voice.hpp"

#include <cstring>

void Voice::initialize(VoiceId id, std::shared_ptr<SoundBuffer> buffer, float gain, bool looping) {
  _id = id;
  _buffer = std::move(buffer);
  _cursor = 0;
  _gain = gain;
  _looping = looping;
  _state = State::Playing;
  _fadeFramesRemaining = 0;
  _fadeStep = 1.0f;
  _currentFadeGain = 1.0f;
}

void Voice::reset() {
  _id = 0;
  _buffer.reset();
  _cursor = 0;
  _gain = 1.0f;
  _looping = false;
  _state = State::Stopped;
  _fadeFramesRemaining = 0;
  _fadeStep = 1.0f;
  _currentFadeGain = 1.0f;
}

void Voice::mix(float* out, uint32_t frameCount) {
  if (!isActive()) return;

  const float* data = _buffer->data();
  uint32_t channels = _buffer->channels();
  size_t totalFrames = _buffer->totalFrames();

  for (uint32_t i = 0; i < frameCount; ++i) {
    if (_cursor >= totalFrames) {
      if (_looping) {
        _cursor = 0;
      } else {
        break;
      }
    }

    float effectiveGain = _gain * _currentFadeGain;

    for (uint32_t ch = 0; ch < channels; ++ch) {
      size_t sampleIndex = _cursor * channels + ch;
      out[i * channels + ch] += effectiveGain * data[sampleIndex];
    }
    _cursor++;
  }
}

void Voice::beginFadeOut(uint32_t fadeFrames) {
  if (_state == State::Playing && fadeFrames > 0) {
    _state = State::FadingOut;
    _fadeFramesRemaining = fadeFrames;
    _fadeStep = 1.0f / static_cast<float>(fadeFrames);
    _currentFadeGain = 1.0f;
  }
}

void Voice::stepFade() {
  if (_state == State::FadingOut && _fadeFramesRemaining > 0) {
    _currentFadeGain -= _fadeStep;
    _fadeFramesRemaining--;

    if (_fadeFramesRemaining == 0 || _currentFadeGain <= 0.0f) {
      stop();
    }
  }
}

void Voice::setGain(float gain) { _gain = gain; }
float Voice::gain() const { return _gain; }
VoiceId Voice::id() const { return _id; }
bool Voice::isFinished() const { return _state == State::Stopped; }
bool Voice::isActive() const { return _state == State::Playing; }
void Voice::stop() { _state = State::Stopped; }