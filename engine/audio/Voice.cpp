#include "Voice.hpp"

#include <cstring>
#include <iostream>

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

  std::cout << "[Voice] Initialized with frames: " << _buffer->totalFrames() << "\n";
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

void Voice::mix(float* out, uint32_t frameCount) const {
  if (!isActive()) return;

  const float* data = _buffer->data();
  uint32_t channels = _buffer->channels();
  size_t totalFrames = _buffer->totalFrames();

  std::cout << "[Voice] First sample: " << data[0] << "\n";

  size_t localCursor = _cursor;

  for (uint32_t i = 0; i < frameCount; ++i) {
    if (localCursor >= totalFrames) {
      std::cout << "[Voice] Cursor reached end of buffer.\n";
      if (_looping) {
        localCursor = 0;
      } else {
        break;
      }
    }

    float effectiveGain = _gain * _currentFadeGain;

    for (uint32_t ch = 0; ch < channels; ++ch) {
      size_t sampleIndex = localCursor * channels + ch;
      out[i * channels + ch] += effectiveGain * data[sampleIndex];
    }
    localCursor++;
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

void Voice::advanceCursor(uint32_t frameCount) {
  if (!isActive()) {
    return;
  }
  _cursor += frameCount;
  if (_cursor >= _buffer->totalFrames()) {
    if (_looping) {
      _cursor = _cursor % _buffer->totalFrames();
    } else {
      _state = State::Stopped;
    }
  }
}

void Voice::setGain(float gain) { _gain = gain; }
float Voice::gain() const { return _gain; }
VoiceId Voice::id() const { return _id; }
bool Voice::isFinished() const { return _state == State::Stopped; }
bool Voice::isActive() const { return _state == State::Playing; }
void Voice::stop() { _state = State::Stopped; }