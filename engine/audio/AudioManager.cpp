#include "AudioManager.hpp"

#include <miniaudio.h>

#include <algorithm>

#include "../core/logger/logging.hpp"

namespace {

uint32_t framesFor(float seconds) {
  return static_cast<uint32_t>(std::max(0.0f, seconds) * static_cast<float>(SoundBuffer::kSampleRate));
}

}  // namespace

AudioManager::AudioManager() {
  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.sampleRate = SoundBuffer::kSampleRate;
  config.playback.format = ma_format_f32;
  config.playback.channels = SoundBuffer::kChannels;
  config.dataCallback = audioCallback;
  config.pUserData = this;

  // A machine without an output device still runs the game, silently.
  if (ma_device_init(nullptr, &config, &_device) != MA_SUCCESS) {
    JM_LOG_ERROR("[Audio] no playback device; audio disabled");
    return;
  }
  if (ma_device_start(&_device) != MA_SUCCESS) {
    JM_LOG_ERROR("[Audio] failed to start playback device; audio disabled");
    ma_device_uninit(&_device);
    return;
  }
  _deviceStarted = true;
}

AudioManager::~AudioManager() {
  if (_deviceStarted) ma_device_uninit(&_device);
}

void AudioManager::audioCallback(ma_device* device, void* output, const void*, ma_uint32 frameCount) {
  auto* self = static_cast<AudioManager*>(device->pUserData);
  VoiceCommand cmd;
  while (self->_commands.try_dequeue(cmd)) self->_voices.apply(cmd);
  self->_voices.mix(static_cast<float*>(output), frameCount, SoundBuffer::kChannels);
}

void AudioManager::send(VoiceCommand cmd) {
  if (!_deviceStarted) return;  // nothing would ever drain the queue
  if (!_commands.try_enqueue(std::move(cmd))) JM_LOG_WARN("[Audio] command queue full; dropping command");
}

void AudioManager::registerSound(std::initializer_list<std::string_view> names,
                                 std::shared_ptr<SoundBuffer> buffer) {
  // A buffer only _retired holds can't be reached again: no voice plays it and
  // the registry no longer hands it out.
  std::erase_if(_retired, [](const std::shared_ptr<SoundBuffer>& b) { return b.use_count() == 1; });
  if (!buffer) return;
  for (auto name : names) {
    std::shared_ptr<SoundBuffer>& entry = _soundRegistry[AudioHandle(name)];
    if (entry && entry != buffer && std::ranges::find(_retired, entry) == _retired.end()) _retired.push_back(entry);
    entry = buffer;
  }
}

SoundInstanceId AudioManager::play(AudioHandle handle, float gain, bool loop, AudioBus bus) {
  auto it = _soundRegistry.find(handle);
  if (it == _soundRegistry.end()) return 0;
  const SoundInstanceId id = _nextInstanceId.fetch_add(1, std::memory_order_relaxed);
  send({.type = VoiceCommand::Type::Play,
        .instance = id,
        .buffer = it->second,
        .value = gain,
        .looping = loop,
        .bus = bus == AudioBus::Master ? AudioBus::Sfx : bus});
  return id;
}

void AudioManager::stop(SoundInstanceId instance) {
  send({.type = VoiceCommand::Type::Stop, .instance = instance});
}

void AudioManager::fade(SoundInstanceId instance, float durationSeconds) {
  send({.type = VoiceCommand::Type::FadeOut, .instance = instance, .frames = framesFor(durationSeconds)});
}

void AudioManager::setGain(SoundInstanceId instance, float gain) {
  send({.type = VoiceCommand::Type::SetGain, .instance = instance, .value = gain});
}

void AudioManager::fadeOutAll(float durationSeconds) {
  send({.type = VoiceCommand::Type::FadeOutAll, .frames = framesFor(durationSeconds)});
}

void AudioManager::stopAll() {
  send({.type = VoiceCommand::Type::StopAll});
}

void AudioManager::setBusVolume(AudioBus bus, float volume) {
  send({.type = VoiceCommand::Type::SetBusGain, .value = std::clamp(volume, 0.0f, 1.0f), .bus = bus});
}
