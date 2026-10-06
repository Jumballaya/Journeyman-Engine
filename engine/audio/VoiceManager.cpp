#include "VoiceManager.hpp"

#include <cmath>
#include <cstring>

VoiceManager::VoiceManager() { _busGain.fill(1.0f); }

Voice* VoiceManager::find(SoundInstanceId instance) {
  for (Voice& v : _voices) {
    if (v.active() && v.instance() == instance) return &v;
  }
  return nullptr;
}

void VoiceManager::apply(const VoiceCommand& cmd) {
  switch (cmd.type) {
    case VoiceCommand::Type::Play: {
      // Free slot first; when full, steal the oldest-started non-music voice
      // (lowest instance id) so new SFX are never silently dropped.
      Voice* slot = nullptr;
      for (Voice& v : _voices) {
        if (!v.active()) {
          slot = &v;
          break;
        }
        if (v.bus() != AudioBus::Music && (!slot || v.instance() < slot->instance())) slot = &v;
      }
      if (slot) slot->start(cmd.instance, cmd.buffer, cmd.value, cmd.looping, cmd.bus);
      break;
    }
    case VoiceCommand::Type::Stop:
      if (Voice* v = find(cmd.instance)) v->stop();
      break;
    case VoiceCommand::Type::SetGain:
      if (Voice* v = find(cmd.instance)) v->setGain(cmd.value);
      break;
    case VoiceCommand::Type::FadeOut:
      if (Voice* v = find(cmd.instance)) v->fadeOut(cmd.frames);
      break;
    case VoiceCommand::Type::StopAll:
      for (Voice& v : _voices) v.stop();
      break;
    case VoiceCommand::Type::FadeOutAll:
      for (Voice& v : _voices) {
        if (v.active()) v.fadeOut(cmd.frames);
      }
      break;
    case VoiceCommand::Type::SetBusGain:
      _busGain[static_cast<size_t>(cmd.bus)] = cmd.value;
      break;
  }
}

void VoiceManager::mix(float* output, uint32_t frameCount, uint32_t channels) {
  std::memset(output, 0, sizeof(float) * frameCount * channels);
  const float master = _busGain[static_cast<size_t>(AudioBus::Master)];
  for (Voice& v : _voices) {
    if (v.active()) v.mix(output, frameCount, channels, master * _busGain[static_cast<size_t>(v.bus())]);
  }
  // Soft knee limiter: transparent below 0.8, smoothly saturates above, so a
  // burst of simultaneous explosions never hard-clips.
  for (uint32_t i = 0; i < frameCount * channels; ++i) {
    const float x = output[i];
    const float a = std::fabs(x);
    if (a > 0.8f) output[i] = std::copysign(0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f), x);
  }
}

size_t VoiceManager::activeVoiceCount() const {
  size_t n = 0;
  for (const Voice& v : _voices) n += v.active() ? 1 : 0;
  return n;
}
