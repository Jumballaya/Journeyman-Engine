#include "InputsHostFunctions.hpp"

#include <iostream>

#include "../core/app/Application.hpp"
#include "../core/scripting/ScriptManager.hpp"
#include "InputsModule.hpp"

// Static pointers to runtime context
static Application* currentApp = nullptr;
static InputsModule* currentInputsModule = nullptr;

void setInputsHostContext(Application& app, InputsModule& module) {
  currentApp = &app;
  currentInputsModule = &module;
}

void clearInputsHostContext() {
  currentApp = nullptr;
  currentInputsModule = nullptr;
}

m3ApiRawFunction(keyIsPressed) {
  (void)_ctx;
  (void)_mem;

  m3ApiReturnType(bool);
  m3ApiGetArg(int32_t, ptr);
  m3ApiGetArg(int32_t, len);

  if (!currentInputsModule) {
    return "Inputs context missing";
  }

  uint8_t* memory = m3_GetMemory(runtime, nullptr, 0);
  if (!memory) {
    return "Memory context missing";
  }

  std::string key(reinterpret_cast<char*>(memory + ptr), len);

  std::cout << "[script] isKeyPressed: " << key << "\n";

  // CHECK KEY
  //
  //

  m3ApiReturn(false);
}