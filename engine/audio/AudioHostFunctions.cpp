#include "AudioHostFunctions.hpp"

#include <iostream>

#include "../core/app/Engine.hpp"
#include "../core/scripting/ScriptManager.hpp"
#include "AudioManager.hpp"
#include "AudioModule.hpp"
#include "../core/logger/logging.hpp"
#include "../core/scripting/WasmMemory.hpp"

// Static pointers to runtime context
static Engine* currentEngine = nullptr;
static AudioModule* currentAudioModule = nullptr;

void setAudioHostContext(Engine& app, AudioModule& module) {
  currentEngine = &app;
  currentAudioModule = &module;
}

void clearAudioHostContext() {
  currentEngine = nullptr;
  currentAudioModule = nullptr;
}

m3ApiRawFunction(playSound) {
  m3ApiReturnType(uint32_t);
  m3ApiGetArg(int32_t, ptr);
  m3ApiGetArg(int32_t, len);
  m3ApiGetArg(float, gain);
  m3ApiGetArg(int32_t, loopFlag);
  m3ApiGetArg(int32_t, bus);

  auto name = wasm_memory::readString(runtime, ptr, len);
  if (!currentAudioModule || !name) m3ApiReturn(0);
  const auto audioBus = (bus == static_cast<int32_t>(AudioBus::Music)) ? AudioBus::Music : AudioBus::Sfx;
  SoundInstanceId id = currentAudioModule->getAudioManager().play(AudioHandle(*name), gain, loopFlag != 0, audioBus);
  if (id == 0) JM_LOG_WARN("[Audio] play: unknown sound '{}'", *name);
  m3ApiReturn(id);
}

m3ApiRawFunction(setBusVolume) {
  m3ApiGetArg(int32_t, bus);
  m3ApiGetArg(float, volume);
  if (currentAudioModule && bus >= 0 && bus < static_cast<int32_t>(AudioBus::Count)) {
    currentAudioModule->getAudioManager().setBusVolume(static_cast<AudioBus>(bus), volume);
  }
  m3ApiSuccess();
}

m3ApiRawFunction(stopAllSounds) {
  m3ApiGetArg(float, fadeSeconds);
  if (currentAudioModule) currentAudioModule->getAudioManager().fadeOutAll(fadeSeconds);
  m3ApiSuccess();
}

m3ApiRawFunction(stopSound) {
  m3ApiReturnType(void);
  (void)_ctx;
  (void)_mem;
  (void)raw_return;
  (void)runtime;

  m3ApiGetArg(uint32_t, instanceId);
  if (!currentAudioModule) {
    return "Audio context missing";
  }
  currentAudioModule->getAudioManager().stop(instanceId);
  m3ApiSuccess();
}

m3ApiRawFunction(fadeOutSound) {
  m3ApiReturnType(void);
  (void)_ctx;
  (void)_mem;
  (void)raw_return;
  (void)runtime;

  m3ApiGetArg(uint32_t, instanceId);
  m3ApiGetArg(float, durationSeconds);
  if (!currentAudioModule) {
    return "Audio context missing";
  }
  currentAudioModule->getAudioManager().fade(instanceId, durationSeconds);
  m3ApiSuccess();
}

m3ApiRawFunction(setGainSound) {
  m3ApiReturnType(void);
  (void)_ctx;
  (void)_mem;
  (void)raw_return;
  (void)runtime;

  m3ApiGetArg(uint32_t, instanceId);
  m3ApiGetArg(float, gain);
  if (!currentAudioModule) {
    return "Audio context missing";
  }
  currentAudioModule->getAudioManager().setGain(instanceId, gain);
  m3ApiSuccess();
}