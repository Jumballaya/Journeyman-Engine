#include "VoiceManager.hpp"

#include <algorithm>
#include <cstring>

VoiceManager::VoiceManager() : _commandQueue(1024), _voices(256) {}

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

  for (auto& voice : _voices.activeVoices()) {
    if (voice->isActive()) {
      voice->stepFade();
      voice->advanceCursor(framesPerUpdate);
    }
  }
}

void VoiceManager::mix(float* output, uint32_t frameCount, uint32_t channels) const {
  std::memset(output, 0, sizeof(float) * frameCount * channels);

  for (const auto& voice : _voices.activeVoices()) {
    if (voice->isActive()) {
      voice->mix(output, frameCount);
    }
  }
}

std::vector<VoiceId> VoiceManager::getActiveVoiceIds() const {
  std::vector<VoiceId> out;
  out.reserve(_voices.activeVoiceCount());
  for (const auto& voice : _voices.activeVoices()) {
    out.push_back(voice->id());
  }
  return out;
}

void VoiceManager::handleCommand(const VoiceCommand& cmd) {
  switch (cmd.type) {
    case VoiceCommand::Type::Play: {
      _voices.acquireVoice(cmd.buffer, cmd.gain, cmd.looping);
      break;
    }
    case VoiceCommand::Type::Stop: {
      Voice* v = _voices.getVoice(cmd.targetVoiceId);
      if (v && v->isActive()) {
        v->stop();
      }
      break;
    }
    case VoiceCommand::Type::SetGain: {
      Voice* v = _voices.getVoice(cmd.targetVoiceId);
      if (v && v->isActive()) {
        v->setGain(cmd.gain);
      }
      break;
    }
    case VoiceCommand::Type::FadeOut: {
      Voice* v = _voices.getVoice(cmd.targetVoiceId);
      if (v && v->isActive()) {
        v->beginFadeOut(cmd.fadeOutFrames);
      }
      break;
    }
  }
}