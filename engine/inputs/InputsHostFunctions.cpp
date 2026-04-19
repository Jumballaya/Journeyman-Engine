#include "InputsHostFunctions.hpp"

#include <iostream>

#include "../core/app/Engine.hpp"
#include "../core/scripting/ScriptManager.hpp"
#include "InputsModule.hpp"

// Static pointers to runtime context
static Engine* currentEngine = nullptr;
static InputsModule* currentInputsModule = nullptr;

void setInputsHostContext(Engine& app, InputsModule& module) {
  currentEngine = &app;
  currentInputsModule = &module;
}

void clearInputsHostContext() {
  currentEngine = nullptr;
  currentInputsModule = nullptr;
}

m3ApiRawFunction(jmKeyIsPressed) {
  (void)_ctx;
  (void)_mem;

  m3ApiReturnType(int32_t);
  m3ApiGetArg(uint32_t, keycode);

  if (!currentInputsModule) {
    m3ApiReturn(0);
  }
  bool pressed = currentInputsModule->getManager().keyIsPressed(static_cast<inputs::Key>(keycode));
  m3ApiReturn(pressed ? 1 : 0);
}

m3ApiRawFunction(jmKeyIsReleased) {
  (void)_ctx;
  (void)_mem;

  m3ApiReturnType(int32_t);
  m3ApiGetArg(uint32_t, keycode);

  if (!currentInputsModule) {
    m3ApiReturn(0);
  }
  bool pressed = currentInputsModule->getManager().keyIsReleased(static_cast<inputs::Key>(keycode));
  m3ApiReturn(pressed ? 1 : 0);
}

m3ApiRawFunction(jmKeyIsDown) {
  (void)_ctx;
  (void)_mem;

  m3ApiReturnType(int32_t);
  m3ApiGetArg(uint32_t, keycode);

  if (!currentInputsModule) {
    m3ApiReturn(0);
  }
  bool pressed = currentInputsModule->getManager().keyIsDown(static_cast<inputs::Key>(keycode));
  m3ApiReturn(pressed ? 1 : 0);
}