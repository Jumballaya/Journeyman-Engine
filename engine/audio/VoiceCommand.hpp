#pragma once

#include <cstdint>
#include <memory>

#include "SoundBuffer.hpp"
#include "SoundInstance.hpp"
#include "Voice.hpp"

struct VoiceCommand {
  enum class Type {
    Play,
    Stop,
    SetGain,
    FadeOut
  };

  VoiceCommand() = default;

  Type type;
  VoiceId targetVoiceId = 0;
  SoundInstanceId instanceId = 0;

  std::shared_ptr<SoundBuffer> buffer;
  float gain = 1.0f;
  bool looping = false;
  uint32_t fadeOutFrames = 0;

  static VoiceCommand PlayCommand(std::shared_ptr<SoundBuffer> buffer, float gain, bool looping) {
    VoiceCommand cmd;
    cmd.type = Type::Play;
    cmd.buffer = std::move(buffer);
    cmd.gain = gain;
    cmd.looping = looping;
    return cmd;
  }

  static VoiceCommand StopCommand(VoiceId id) {
    VoiceCommand cmd;
    cmd.type = Type::Stop;
    cmd.targetVoiceId = id;
    return cmd;
  }

  static VoiceCommand SetGainCommand(VoiceId id, float gain) {
    VoiceCommand cmd;
    cmd.type = Type::SetGain;
    cmd.targetVoiceId = id;
    cmd.gain = gain;
    return cmd;
  }

  static VoiceCommand FadeOutCommand(VoiceId id, uint32_t fadeOutFrames) {
    VoiceCommand cmd;
    cmd.type = Type::FadeOut;
    cmd.targetVoiceId = id;
    cmd.fadeOutFrames = fadeOutFrames;
    return cmd;
  }
};