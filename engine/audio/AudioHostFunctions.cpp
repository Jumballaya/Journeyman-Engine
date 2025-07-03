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

  m3ApiGetArg(int32_t, ptr);
  m3ApiGetArg(int32_t, len);

  if (!currentAudioModule) {
    return "Audio context missing";
  }

  uint8_t* memory = m3_GetMemory(runtime, nullptr, 0);
  std::string soundName(reinterpret_cast<char*>(memory + ptr), len);

  AudioHandle handle(soundName);
  currentAudioModule->getAudioManager().play(handle);

  m3ApiSuccess();
}