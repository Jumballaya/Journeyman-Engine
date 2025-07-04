#pragma once

#include <cstdint>
#include <vector>

#include "../core/async/LockFreeQueue.hpp"
#include "SoundBuffer.hpp"
#include "SoundInstance.hpp"
#include "Voice.hpp"
#include "VoiceCommand.hpp"
#include "VoicePool.hpp"

class VoiceManager {
 public:
  VoiceManager();
  ~VoiceManager();

  void queueCommand(VoiceCommand cmd);
  void update(std::vector<VoiceId>& finished, std::vector<std::pair<SoundInstanceId, VoiceId>>& started);
  void mix(float* output, uint32_t frameCount, uint32_t channels) const;

  std::vector<VoiceId> getActiveVoiceIds() const;

 private:
  LockFreeQueue<VoiceCommand> _commandQueue;
  VoicePool _voices;

  void handleCommand(const VoiceCommand& cmd, std::vector<VoiceId>& finished, std::vector<std::pair<SoundInstanceId, VoiceId>>& started);
};