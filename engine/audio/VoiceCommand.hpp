#pragma once

#include <cstdint>
#include <memory>

#include "SoundBuffer.hpp"
#include "Voice.hpp"

// Main/worker thread → audio thread messages. Sounds are addressed by the
// SoundInstanceId handed out at play() time, so a stop/fade queued right
// after a play (same frame) still finds its voice.
struct VoiceCommand {
  enum class Type { Play, Stop, SetGain, FadeOut, StopAll, FadeOutAll, SetBusGain };

  Type type = Type::Stop;
  SoundInstanceId instance = 0;
  std::shared_ptr<SoundBuffer> buffer;
  float value = 1.0f;          // gain, or bus gain
  uint32_t frames = 0;         // fade length
  bool looping = false;
  AudioBus bus = AudioBus::Sfx;
};
