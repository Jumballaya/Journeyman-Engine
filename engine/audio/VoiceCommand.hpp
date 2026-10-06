#pragma once

#include <cstdint>
#include <memory>

#include "SoundBuffer.hpp"
#include "Voice.hpp"

// Messages to the audio thread, addressed by the id play() returned, so a stop
// queued right after its play still finds the voice.
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
