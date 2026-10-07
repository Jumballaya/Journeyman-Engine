#pragma once

#include <cstdint>
#include <memory>

#include "SoundBuffer.hpp"

using SoundInstanceId = uint32_t;

// Mix groups. Every voice plays on one bus; the bus volume (times Master)
// scales it. Scripts expose these as Audio.Bus.
enum class AudioBus : uint8_t { Master = 0, Music = 1, Sfx = 2, Count = 3 };

// One playing sound. Owned and touched exclusively by the audio thread.
class Voice {
 public:
  void start(SoundInstanceId instance, std::shared_ptr<SoundBuffer> buffer, float gain,
             bool looping, AudioBus bus);
  // Never frees the buffer: AudioManager holds every buffer a voice can play.
  void stop() { _buffer.reset(); }

  // Adds this voice into `out` (interleaved stereo). Advances fades per
  // sample; stops itself when a one-shot ends or a fade reaches silence.
  void mix(float* out, uint32_t frameCount, uint32_t channels, float busGain);

  void setGain(float gain) { _gain = gain; }
  void fadeOut(uint32_t frames);

  bool active() const { return _buffer != nullptr; }
  SoundInstanceId instance() const { return _instance; }
  AudioBus bus() const { return _bus; }

 private:
  SoundInstanceId _instance = 0;
  std::shared_ptr<SoundBuffer> _buffer;
  size_t _cursor = 0;
  float _gain = 1.0f;
  bool _looping = false;
  AudioBus _bus = AudioBus::Sfx;
  float _fadeGain = 1.0f;
  float _fadeStep = 0.0f;  // subtracted from _fadeGain per frame; 0 = no fade
};
