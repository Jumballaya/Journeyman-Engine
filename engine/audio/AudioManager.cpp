#include "AudioManager.hpp"

#include <miniaudio.h>

#include <algorithm>
#include <cstring>

#include "../core/logger/logging.hpp"

AudioManager::AudioManager() {
  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.sampleRate = 48000;
  config.playback.format = ma_format_f32;
  config.playback.channels = 2;
  config.dataCallback = audioCallback;
  config.pUserData = this;

  _sampleRate = config.sampleRate;
  _channels = config.playback.channels;

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
  while (self->_commands.try_dequeue(cmd)) {
    self->_voices.apply(cmd);
  }
  self->_voices.mix(static_cast<float*>(output), frameCount, self->_channels);
}

void AudioManager::send(VoiceCommand cmd) {
  if (!_commands.try_enqueue(std::move(cmd))) {
    JM_LOG_WARN("[Audio] command queue full; dropping command");
  }
}

uint32_t AudioManager::framesFor(float seconds) const {
  return static_cast<uint32_t>(std::max(0.0f, seconds) * static_cast<float>(_sampleRate));
}

void AudioManager::registerSound(std::initializer_list<std::string_view> names,
                                 std::shared_ptr<SoundBuffer> buffer) {
  for (auto name : names) _soundRegistry[AudioHandle(name)] = buffer;
}

SoundInstanceId AudioManager::play(AudioHandle handle, float gain, bool loop, AudioBus bus) {
  auto it = _soundRegistry.find(handle);
  if (it == _soundRegistry.end()) return 0;

  VoiceCommand cmd;
  cmd.type = VoiceCommand::Type::Play;
  cmd.instance = _nextInstanceId.fetch_add(1, std::memory_order_relaxed);
  cmd.buffer = it->second;
  cmd.value = gain;
  cmd.looping = loop;
  cmd.bus = bus == AudioBus::Master ? AudioBus::Sfx : bus;
  const SoundInstanceId id = cmd.instance;
  send(std::move(cmd));
  return id;
}

void AudioManager::stop(SoundInstanceId instance) {
  VoiceCommand cmd;
  cmd.type = VoiceCommand::Type::Stop;
  cmd.instance = instance;
  send(std::move(cmd));
}

void AudioManager::fade(SoundInstanceId instance, float durationSeconds) {
  VoiceCommand cmd;
  cmd.type = VoiceCommand::Type::FadeOut;
  cmd.instance = instance;
  cmd.frames = framesFor(durationSeconds);
  send(std::move(cmd));
}

void AudioManager::setGain(SoundInstanceId instance, float gain) {
  VoiceCommand cmd;
  cmd.type = VoiceCommand::Type::SetGain;
  cmd.instance = instance;
  cmd.value = gain;
  send(std::move(cmd));
}

void AudioManager::fadeOutAll(float durationSeconds) {
  VoiceCommand cmd;
  cmd.type = VoiceCommand::Type::FadeOutAll;
  cmd.frames = framesFor(durationSeconds);
  send(std::move(cmd));
}

void AudioManager::stopAll() {
  VoiceCommand cmd;
  cmd.type = VoiceCommand::Type::StopAll;
  send(std::move(cmd));
}

void AudioManager::setBusVolume(AudioBus bus, float volume) {
  VoiceCommand cmd;
  cmd.type = VoiceCommand::Type::SetBusGain;
  cmd.bus = bus;
  cmd.value = std::clamp(volume, 0.0f, 1.0f);
  send(std::move(cmd));
}
