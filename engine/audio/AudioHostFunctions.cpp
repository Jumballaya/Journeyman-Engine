#include "AudioHostFunctions.hpp"

#include <iostream>

#include "../core/app/Application.hpp"
#include "../core/scripting/ScriptManager.hpp"
#include "AudioManager.hpp"
#include "AudioModule.hpp"

// Static pointers to runtime context
static Application* currentApp = nullptr;
static AudioModule* currentAudioModule = nullptr;

void setAudioHostContext(Application& app, AudioModule& module) {
  currentApp = &app;
  currentAudioModule = &module;
}

void clearAudioHostContext() {
  currentApp = nullptr;
  currentAudioModule = nullptr;
}

m3ApiRawFunction(playSound) {
  (void)_ctx;
  (void)_mem;

  m3ApiReturnType(uint32_t);
  m3ApiGetArg(int32_t, ptr);
  m3ApiGetArg(int32_t, len);
  m3ApiGetArg(float, gain);
  m3ApiGetArg(int32_t, loopFlag);

  if (!currentAudioModule) {
    return "Audio context missing";
  }

  uint8_t* memory = m3_GetMemory(runtime, nullptr, 0);
  if (!memory) {
    return "Memory context missing";
  }

  std::string soundName(reinterpret_cast<char*>(memory + ptr), len);

  AudioHandle handle(soundName);
  bool loop = loopFlag != 0;

  SoundInstanceId instanceId = currentAudioModule->getAudioManager().play(handle, gain, loop);

  m3ApiReturn(instanceId);
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