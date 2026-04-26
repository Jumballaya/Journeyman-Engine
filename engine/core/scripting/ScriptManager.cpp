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
  // Eagerly parse + immediately free the module to surface format errors at
  // load time. The actual module used by each instance is parsed FRESH in
  // createInstance — wasm3's m3_LoadModule transfers module ownership to a
  // runtime, so a single parsed module can only be bound to one runtime, and
  // sharing one parse across multiple instances of the same script would
  // fail with "module already bound to a runtime".
  IM3Module module = nullptr;
  M3Result result = m3_ParseModule(_env, &module, wasmBinary.data(), wasmBinary.size());
  if (result != m3Err_none) {
    throw std::runtime_error(std::string("Failed to parse wasm module: ") + result);
  }
  m3_FreeModule(module);

  LoadedScript script;
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

  // Parse a fresh module per instance. ScriptInstance takes ownership via
  // m3_LoadModule; on construction failure the constructor frees the module
  // (and the runtime, if it was allocated).
  IM3Module module = nullptr;
  M3Result parseResult = m3_ParseModule(_env, &module, script->binary.data(), script->binary.size());
  if (parseResult != m3Err_none) {
    JM_LOG_ERROR("[ScriptManager] createInstance: parse failed for asset id {}: {}",
                 scriptAsset.id, parseResult);
    return ScriptInstanceHandle{};
  }

  auto instanceHandle = generateScriptInstanceHandle();
  try {
    _instances.try_emplace(instanceHandle, instanceHandle, scriptAsset, eid, _env, module,
                           script->imports, _hostFunctions);
  } catch (const std::exception& e) {
    // ScriptInstance's constructor freed the module + runtime on its way out.
    JM_LOG_ERROR("[ScriptManager] createInstance failed for asset id {}: {}",
                 scriptAsset.id, e.what());
    return ScriptInstanceHandle{};
  }
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
