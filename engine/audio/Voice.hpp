#pragma once

#include <cstddef>
#include <memory>

#include "SoundBuffer.hpp"

class Voice {
 public:
  Voice(std::shared_ptr<SoundBuffer> buffer);

  bool isFinished() const;
  void mix(float* out, uint32_t frameCount);

  std::shared_ptr<SoundBuffer> buffer() const { return _buffer; }
  void setGain(float gain) { _gain = gain; }
  float gain() const { return _gain; }

  void setLooping(bool enabled) { _looping = enabled; }
  bool isLooping() const { return _looping; }

  void fadeOut(uint32_t durationFrames);

 private:
  std::shared_ptr<SoundBuffer> _buffer;
  size_t _cursor = 0;
  float _gain = 1.0f;
  bool _looping = false;

  enum class State {
    Playing,
    FadingOut,
    Finished
  };

  State _state = State::Playing;
  uint32_t _fadeRemaining = 0;
  float _currentGain = 1.0f;
};