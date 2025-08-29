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