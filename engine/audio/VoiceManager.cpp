#include "VoiceManager.hpp"

#include <algorithm>
#include <cstring>

VoiceManager::VoiceManager() : _commandQueue(1024) {}

VoiceManager::~VoiceManager() {
  _commandQueue.shutdown();
}

void VoiceManager::queueCommand(VoiceCommand cmd) {
  _commandQueue.try_enqueue(std::move(cmd));
}

void VoiceManager::update(uint32_t framesPerUpdate) {
  VoiceCommand cmd;
  while (_commandQueue.try_dequeue(cmd)) {
    handleCommand(cmd);
  }

  for (auto& voice : _voices) {
    if (voice.isActive()) {
      voice.stepFade();
      voice.advanceCursor(framesPerUpdate);
    }
  }

  _voices.erase(std::remove_if(_voices.begin(), _voices.end(),
                               [](const Voice& v) { return v.isFinished(); }),
                _voices.end());
}

void VoiceManager::mix(float* output, uint32_t frameCount, uint32_t channels) const {
  std::memset(output, 0, sizeof(float) * frameCount * channels);

  for (const auto& voice : _voices) {
    if (voice.isActive()) {
      voice.mix(output, frameCount);
    }
  }
}

void VoiceManager::handleCommand(const VoiceCommand& cmd) {
  switch (cmd.type) {
    case VoiceCommand::Type::Play: {
      Voice newVoice;
      newVoice.initialize(nextVoiceId++, cmd.buffer, cmd.gain, cmd.looping);
      _voices.push_back(std::move(newVoice));
      break;
    }
    case VoiceCommand::Type::Stop: {
      for (auto& v : _voices) {
        if (v.id() == cmd.targetVoiceId && v.isActive()) {
          v.stop();
        }
      }
      break;
    }
    case VoiceCommand::Type::SetGain: {
      for (auto& v : _voices) {
        if (v.id() == cmd.targetVoiceId && v.isActive()) {
          v.setGain(cmd.gain);
        }
      }
      break;
    }
    case VoiceCommand::Type::FadeOut: {
      for (auto& v : _voices) {
        if (v.id() == cmd.targetVoiceId && v.isActive()) {
          v.beginFadeOut(cmd.fadeOutFrames);
        }
      }
      break;
    }
  }
}