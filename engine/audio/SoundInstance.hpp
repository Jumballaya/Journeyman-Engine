#pragma once

#include <cstdint>
#include <optional>

#include "AudioHandle.hpp"
#include "Voice.hpp"

using SoundInstanceId = uint32_t;

struct SoundInstance {
  SoundInstanceId id;
  AudioHandle handle;
  std::optional<VoiceId> voiceId;
};