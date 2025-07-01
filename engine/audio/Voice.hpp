#pragma once

#include <cstddef>
#include <memory>

#include "SoundBuffer.hpp"
#include "VoiceCommand.hpp"

class Voice {
 public:
  Voice() = default;

  void initialize(VoiceId id, std::shared_ptr<SoundBuffer> buffer, float gain, bool looping);
  void reset();

  VoiceId id() const;
  bool isFinished() const;
  bool isActive() const;

  void mix(float* out, uint32_t frameCount) const;
  void advanceCursor(uint32_t frameCount);

  void setGain(float gain);
  float gain() const;

  void beginFadeOut(uint32_t fadeFrames);
  void stepFade();

  void stop();

 private:
  VoiceId _id;
  std::shared_ptr<SoundBuffer> _buffer;
  size_t _cursor = 0;

  float _gain = 1.0f;
  bool _looping = false;

  enum class State {
    Playing,
    FadingOut,
    Stopped
  };
  State _state = State::Playing;

  uint32_t _fadeFramesRemaining = 0;
  float _fadeStep = 1.0f;
  float _currentGain = 1.0f;
  float _currentFadeGain = 1.0f;
};