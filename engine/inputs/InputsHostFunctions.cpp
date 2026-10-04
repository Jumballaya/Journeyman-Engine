#include "InputsHostFunctions.hpp"

#include <wasm3.h>

#include "../core/app/Engine.hpp"
#include "../core/scripting/HostFunction.hpp"
#include "../core/scripting/ScriptManager.hpp"
#include "../core/scripting/WasmMemory.hpp"
#include "InputsModule.hpp"

static InputsModule* currentInputsModule = nullptr;

void setInputsHostContext(Engine&, InputsModule& module) {
  currentInputsModule = &module;
}

void clearInputsHostContext() {
  currentInputsModule = nullptr;
}

namespace {
bool validKey(uint32_t keycode) { return keycode < inputs::Key::Key_Count; }
}  // namespace

m3ApiRawFunction(jmKeyIsPressed) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(uint32_t, keycode);
  if (!currentInputsModule || !validKey(keycode)) m3ApiReturn(0);
  m3ApiReturn(currentInputsModule->getManager().keyIsPressed(static_cast<inputs::Key>(keycode)) ? 1 : 0);
}

m3ApiRawFunction(jmKeyIsReleased) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(uint32_t, keycode);
  if (!currentInputsModule || !validKey(keycode)) m3ApiReturn(0);
  m3ApiReturn(currentInputsModule->getManager().keyIsReleased(static_cast<inputs::Key>(keycode)) ? 1 : 0);
}

m3ApiRawFunction(jmKeyIsDown) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(uint32_t, keycode);
  if (!currentInputsModule || !validKey(keycode)) m3ApiReturn(0);
  m3ApiReturn(currentInputsModule->getManager().keyIsDown(static_cast<inputs::Key>(keycode)) ? 1 : 0);
}

// Action queries: (namePtr, nameLen, query) where query 0 = down,
// 1 = pressed, 2 = released. Returns 0/1.
m3ApiRawFunction(jmActionState) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, namePtr);
  m3ApiGetArg(int32_t, nameLen);
  m3ApiGetArg(int32_t, query);
  auto name = wasm_memory::readString(runtime, namePtr, nameLen);
  if (!currentInputsModule || !name) m3ApiReturn(0);
  const auto& keys = currentInputsModule->getManager();
  const auto& actions = currentInputsModule->getActions();
  bool result = query == 1 ? actions.pressed(*name, keys)
              : query == 2 ? actions.released(*name, keys)
                           : actions.down(*name, keys);
  m3ApiReturn(result ? 1 : 0);
}

m3ApiRawFunction(jmActionValue) {
  m3ApiReturnType(float);
  m3ApiGetArg(int32_t, namePtr);
  m3ApiGetArg(int32_t, nameLen);
  auto name = wasm_memory::readString(runtime, namePtr, nameLen);
  if (!currentInputsModule || !name) m3ApiReturn(0.0f);
  m3ApiReturn(currentInputsModule->getActions().value(*name, currentInputsModule->getManager()));
}

m3ApiRawFunction(jmActionBind) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, namePtr);
  m3ApiGetArg(int32_t, nameLen);
  m3ApiGetArg(int32_t, controlPtr);
  m3ApiGetArg(int32_t, controlLen);
  auto name = wasm_memory::readString(runtime, namePtr, nameLen);
  auto control = wasm_memory::readString(runtime, controlPtr, controlLen);
  if (!currentInputsModule || !name || !control) m3ApiReturn(0);
  m3ApiReturn(currentInputsModule->getActions().bind(*name, *control) ? 1 : 0);
}

m3ApiRawFunction(jmActionUnbind) {
  m3ApiGetArg(int32_t, namePtr);
  m3ApiGetArg(int32_t, nameLen);
  auto name = wasm_memory::readString(runtime, namePtr, nameLen);
  if (currentInputsModule && name) currentInputsModule->getActions().unbind(*name);
  m3ApiSuccess();
}

m3ApiRawFunction(jmGamepadConnected) {
  m3ApiReturnType(int32_t);
  m3ApiReturn(currentInputsModule && currentInputsModule->getActions().gamepadConnected() ? 1 : 0);
}

void registerInputsHostFunctions(ScriptManager& scripts) {
  const HostFunction functions[] = {
      {"env", "__jmKeyIsPressed", "i(i)", &jmKeyIsPressed},
      {"env", "__jmKeyIsReleased", "i(i)", &jmKeyIsReleased},
      {"env", "__jmKeyIsDown", "i(i)", &jmKeyIsDown},
      {"env", "__jmActionState", "i(iii)", &jmActionState},
      {"env", "__jmActionValue", "f(ii)", &jmActionValue},
      {"env", "__jmActionBind", "i(iiii)", &jmActionBind},
      {"env", "__jmActionUnbind", "v(ii)", &jmActionUnbind},
      {"env", "__jmGamepadConnected", "i()", &jmGamepadConnected},
  };
  for (const auto& fn : functions) scripts.registerHostFunction(fn.name, fn);
}
