#include "ScriptManager.hpp"

#include <stdexcept>
#include <string>

#include "../app/Engine.hpp"
#include "../logger/logging.hpp"
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
}

void ScriptManager::initialize(Engine& app) { (void)app; }

void ScriptManager::registerHostFunction(const std::string& name, const HostFunction& hostFunction) {
  _hostFunctions.emplace(name, hostFunction);
}

void ScriptManager::loadScript(AssetHandle scriptAsset,
                               const std::vector<uint8_t>& wasmBinary,
                               const std::vector<std::string>& imports) {
  IM3Module module = nullptr;
  M3Result result = m3_ParseModule(_env, &module, wasmBinary.data(), wasmBinary.size());
  if (result != m3Err_none) {
    throw std::runtime_error(std::string("Failed to parse wasm module: ") + result);
  }

  LoadedScript script;
  script.module = module;
  script.binary = wasmBinary;
  script.imports = imports;

  _scripts.insert(scriptAsset, std::move(script));
}

ScriptInstanceHandle ScriptManager::createInstance(AssetHandle scriptAsset, EntityId eid) {
  const LoadedScript* script = _scripts.get(scriptAsset);
  if (!script) {
    JM_LOG_ERROR("[ScriptManager] createInstance: no script loaded for asset id {}", scriptAsset.id);
    return ScriptInstanceHandle{};
  }
  auto instanceHandle = generateScriptInstanceHandle();
  _instances.try_emplace(instanceHandle, instanceHandle, scriptAsset, eid, _env, *script, _hostFunctions);
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

ScriptInstanceHandle ScriptManager::generateScriptInstanceHandle() {
  ScriptInstanceHandle handle = _nextScriptInstanceHandle;
  _nextScriptInstanceHandle.id++;
  return handle;
}

const LoadedScript* ScriptManager::getScript(AssetHandle scriptAsset) const {
  return _scripts.get(scriptAsset);
}
