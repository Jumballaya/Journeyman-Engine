#include "UIHostFunctions.hpp"

#include <wasm3.h>

#include "../core/scripting/HostFunction.hpp"
#include "../core/scripting/ScriptManager.hpp"
#include "../core/scripting/WasmMemory.hpp"
#include "UIModule.hpp"

// Element ids are looked up across every live document, so scripts don't
// need a document handle: `UI.setText("score", "1200")` updates the HUD
// wherever it is. All functions return 1 if some element matched.

namespace {
UIModule* s_ui = nullptr;
}

void setUIHostContext(UIModule* module) { s_ui = module; }

m3ApiRawFunction(jmUISetText) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, idPtr);
  m3ApiGetArg(int32_t, idLen);
  m3ApiGetArg(int32_t, textPtr);
  m3ApiGetArg(int32_t, textLen);
  auto id = wasm_memory::readString(runtime, idPtr, idLen);
  auto text = wasm_memory::readString(runtime, textPtr, textLen);
  if (!s_ui || !id || !text) m3ApiReturn(0);
  m3ApiReturn(s_ui->forEachDocument([&](UIDocument& d) { return d.setText(*id, *text); }) ? 1 : 0);
}

m3ApiRawFunction(jmUISetClass) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, idPtr);
  m3ApiGetArg(int32_t, idLen);
  m3ApiGetArg(int32_t, clsPtr);
  m3ApiGetArg(int32_t, clsLen);
  m3ApiGetArg(int32_t, on);
  auto id = wasm_memory::readString(runtime, idPtr, idLen);
  auto cls = wasm_memory::readString(runtime, clsPtr, clsLen);
  if (!s_ui || !id || !cls) m3ApiReturn(0);
  m3ApiReturn(s_ui->forEachDocument([&](UIDocument& d) { return d.setClass(*id, *cls, on != 0); }) ? 1 : 0);
}

m3ApiRawFunction(jmUISetStyle) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, idPtr);
  m3ApiGetArg(int32_t, idLen);
  m3ApiGetArg(int32_t, propPtr);
  m3ApiGetArg(int32_t, propLen);
  m3ApiGetArg(int32_t, valuePtr);
  m3ApiGetArg(int32_t, valueLen);
  auto id = wasm_memory::readString(runtime, idPtr, idLen);
  auto prop = wasm_memory::readString(runtime, propPtr, propLen);
  auto value = wasm_memory::readString(runtime, valuePtr, valueLen);
  if (!s_ui || !id || !prop || !value) m3ApiReturn(0);
  m3ApiReturn(s_ui->forEachDocument([&](UIDocument& d) { return d.setStyle(*id, *prop, *value); }) ? 1 : 0);
}

m3ApiRawFunction(jmUISetAttribute) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, idPtr);
  m3ApiGetArg(int32_t, idLen);
  m3ApiGetArg(int32_t, namePtr);
  m3ApiGetArg(int32_t, nameLen);
  m3ApiGetArg(int32_t, valuePtr);
  m3ApiGetArg(int32_t, valueLen);
  auto id = wasm_memory::readString(runtime, idPtr, idLen);
  auto name = wasm_memory::readString(runtime, namePtr, nameLen);
  auto value = wasm_memory::readString(runtime, valuePtr, valueLen);
  if (!s_ui || !id || !name || !value) m3ApiReturn(0);
  m3ApiReturn(s_ui->forEachDocument([&](UIDocument& d) { return d.setAttribute(*id, *name, *value); }) ? 1 : 0);
}

m3ApiRawFunction(jmUIExists) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, idPtr);
  m3ApiGetArg(int32_t, idLen);
  auto id = wasm_memory::readString(runtime, idPtr, idLen);
  if (!s_ui || !id) m3ApiReturn(0);
  m3ApiReturn(s_ui->forEachDocument([&](UIDocument& d) { return d.has(*id); }) ? 1 : 0);
}

void registerUIHostFunctions(ScriptManager& scripts) {
  const HostFunction functions[] = {
      {"env", "__jmUISetText", "i(iiii)", &jmUISetText},
      {"env", "__jmUISetClass", "i(iiiii)", &jmUISetClass},
      {"env", "__jmUISetStyle", "i(iiiiii)", &jmUISetStyle},
      {"env", "__jmUISetAttribute", "i(iiiiii)", &jmUISetAttribute},
      {"env", "__jmUIExists", "i(ii)", &jmUIExists},
  };
  for (const auto& fn : functions) scripts.registerHostFunction(fn.name, fn);
}
