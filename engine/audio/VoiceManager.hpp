#pragma once

#include <cstdint>
#include <vector>

#include "../core/async/LockFreeQueue.hpp"
#include "SoundBuffer.hpp"
#include "Voice.hpp"
#include "VoiceCommand.hpp"

class VoiceManager {
 public:
  VoiceManager();
  ~VoiceManager();

  void queueCommand(VoiceCommand cmd);
  void update(uint32_t framesPerUpdate);
  void mix(float* output, uint32_t frameCount, uint32_t channels) const;

 private:
  VoiceId nextVoiceId = 1;
  std::vector<Voice> _voices;
  LockFreeQueue<VoiceCommand> _commandQueue;

  void handleCommand(const VoiceCommand& cmd);
};