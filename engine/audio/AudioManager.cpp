#include "AudioManager.hpp"

#include <miniaudio.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

AudioManager::AudioManager() {
  ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.sampleRate = 48000;
  config.playback.format = ma_format_f32;
  config.playback.channels = 2;
  config.dataCallback = audioCallback;
  config.pUserData = this;

  _sampleRate = config.sampleRate;
  _channels = config.playback.channels;

  if (ma_device_init(nullptr, &config, &_device) != MA_SUCCESS) {
    throw std::runtime_error("Failed to initialize audio device");
  }

  if (ma_device_start(&_device) != MA_SUCCESS) {
    throw std::runtime_error("Failed to start audio device");
  }

  if (!ma_device_is_started(&_device)) {
    throw std::runtime_error("Audio device failed to start properly.");
  }
}

AudioManager::~AudioManager() {
  ma_device_uninit(&_device);
}

uint32_t AudioManager::getSampleRate() const {
  return _sampleRate;
}

void AudioManager::audioCallback(ma_device* device, void* output, const void*, ma_uint32 frameCount) {
  auto* self = static_cast<AudioManager*>(device->pUserData);
  std::memset(output, 0, sizeof(float) * frameCount * self->_channels);
  self->_voiceManager.mix(static_cast<float*>(output), frameCount, self->_channels);
}

void AudioManager::mix(float* output, uint32_t frameCount, uint32_t channels) {
  _voiceManager.mix(output, frameCount, channels);
}

AudioHandle AudioManager::registerSound(std::string name, std::shared_ptr<SoundBuffer> buffer) {
  AudioHandle handle(name);
  _soundRegistry[handle] = std::move(buffer);
  return handle;
}

void AudioManager::play(AudioHandle handle, float gain, bool loop) {
  auto it = _soundRegistry.find(handle);
  if (it == _soundRegistry.end()) {
    return;
  }

  _voiceManager.queueCommand(VoiceCommand::PlayCommand(it->second, gain, loop));
}

void AudioManager::fadeOutAll(float durationSeconds) {
  uint32_t durationFrames = static_cast<uint32_t>(durationSeconds * _sampleRate);
  for (VoiceId id : _voiceManager.getActiveVoiceIds()) {
    _voiceManager.queueCommand(VoiceCommand::FadeOutCommand(id, durationFrames));
  }
}

void AudioManager::setGainAll(float gain) {
  for (VoiceId id : _voiceManager.getActiveVoiceIds()) {
    _voiceManager.queueCommand(VoiceCommand::SetGainCommand(id, gain));
  }
}

void AudioManager::stopAll() {
  for (VoiceId id : _voiceManager.getActiveVoiceIds()) {
    _voiceManager.queueCommand(VoiceCommand::StopCommand(id));
  }
}

void AudioManager::update() {
  _voiceManager.update();
}