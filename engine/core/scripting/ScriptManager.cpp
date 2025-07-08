#include "ScriptManager.hpp"

#include <stdexcept>
#include <string>

#include "../app/Application.hpp"
#include "HostFunctions.hpp"
#include "ScriptInstance.hpp"

ScriptManager::ScriptManager() {
  _env = m3_NewEnvironment();
  if (!_env) {
    throw std::runtime_error("unable to create wasm3 environment");
  }
}
ScriptManager::~ScriptManager() {
  if (_env) {
    m3_FreeEnvironment(_env);
  }
  clearHostContext();
}

void ScriptManager::initialize(Application& app) {
  _hostFunctions["__jmLog"] = {"env", "__jmLog", "v(ii)", &log};
  _hostFunctions["abort"] = {"env", "abort", "v(iiii)", &abort};
  setHostContext(app);
}

void ScriptManager::registerHostFunction(const std::string& name, const HostFunction& hostFunction) {
  _hostFunctions.emplace(name, hostFunction);
}

ScriptHandle ScriptManager::loadScript(const std::vector<uint8_t>& wasmBinary, std::vector<std::string>& imports) {
  IM3Module module = nullptr;
  M3Result result = m3_ParseModule(_env, &module, wasmBinary.data(), wasmBinary.size());
  if (result != m3Err_none) {
    throw std::runtime_error(std::string("Failed to parse wasm module: ") + result);
  }
  LoadedScript script;
  script.module = module;
  script.binary = wasmBinary;
  script.imports = imports;

  ScriptHandle handle = generateScriptHandle();
  _scripts[handle] = std::move(script);

  return handle;
}

ScriptInstanceHandle ScriptManager::createInstance(ScriptHandle handle) {
  if (!_scripts.contains(handle)) {
    throw std::runtime_error("Invalid ScriptHandle");
  }
  const LoadedScript& script = _scripts[handle];
  auto instanceHandle = generateScriptInstanceHandle();

  _instances.emplace(instanceHandle, ScriptInstance(instanceHandle, _env, script, _hostFunctions));
  return instanceHandle;
}

void ScriptManager::updateInstance(ScriptInstanceHandle& handle, float dt) {
  auto it = _instances.find(handle);
  if (it == _instances.end()) {
    throw std::runtime_error("Invalid ScriptInstanceHandle");
  }
  it->second.update(dt);
}

ScriptInstance* ScriptManager::getInstance(ScriptInstanceHandle handle) {
  auto it = _instances.find(handle);
  if (it == _instances.end()) {
    return nullptr;
  }
  return &it->second;
}

void ScriptManager::destroyInstance(ScriptInstanceHandle handle) {
  auto it = _instances.find(handle);
  if (it != _instances.end()) {
    _instances.erase(it);
  }
}

ScriptHandle ScriptManager::generateScriptHandle() {
  ScriptHandle handle = _nextScriptHandle;
  _nextScriptHandle.id++;
  return handle;
}

ScriptInstanceHandle ScriptManager::generateScriptInstanceHandle() {
  ScriptInstanceHandle handle = _nextScriptInstanceHandle;
  _nextScriptInstanceHandle.id++;
  return handle;
}
