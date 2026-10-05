#include "ScriptManager.hpp"

#include <stdexcept>
#include <string>

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

void ScriptManager::loadScript(AssetHandle scriptAsset, const std::vector<uint8_t>& wasmBinary,
                               std::string path) {
  // Parse now only to report errors at load time: a wasm3 module binds to one
  // runtime, so each instance parses its own copy.
  IM3Module module = nullptr;
  M3Result result = m3_ParseModule(_env, &module, wasmBinary.data(), wasmBinary.size());
  if (result != m3Err_none) {
    throw std::runtime_error(std::string("Failed to parse wasm module: ") + result);
  }
  m3_FreeModule(module);

  LoadedScript script;
  script.path = std::move(path);
  script.binary = wasmBinary;

  _scripts.insert(scriptAsset, std::move(script));
}

ScriptInstanceHandle ScriptManager::createInstance(AssetHandle scriptAsset, EntityId eid,
                                                   nlohmann::json params) {
  const LoadedScript* script = _scripts.get(scriptAsset);
  if (!script) {
    JM_LOG_ERROR("[ScriptManager] createInstance: no script loaded for asset id {}", scriptAsset.id);
    return ScriptInstanceHandle{};
  }

  // ScriptInstance owns the module from here, even if construction throws.
  IM3Module module = nullptr;
  M3Result parseResult = m3_ParseModule(_env, &module, script->binary.data(), script->binary.size());
  if (parseResult != m3Err_none) {
    JM_LOG_ERROR("[ScriptManager] createInstance: parse failed for asset id {}: {}",
                 scriptAsset.id, parseResult);
    return ScriptInstanceHandle{};
  }

  auto instanceHandle = generateScriptInstanceHandle();
  try {
    _instances.try_emplace(instanceHandle, instanceHandle, scriptAsset, script->path, eid, _env, module,
                           _hostFunctions, std::move(params));
  } catch (const std::exception& e) {
    // ScriptInstance's constructor freed the module + runtime on its way out.
    JM_LOG_ERROR("[ScriptManager] {} failed to start: {}", script->path, e.what());
    return ScriptInstanceHandle{};
  }
  return instanceHandle;
}

ScriptInstance* ScriptManager::getInstance(ScriptInstanceHandle handle) {
  auto it = _instances.find(handle);
  if (it == _instances.end()) {
    return nullptr;
  }
  return &it->second;
}

void ScriptManager::destroyInstance(ScriptInstanceHandle handle) {
  _instances.erase(handle);
}

ScriptInstanceHandle ScriptManager::generateScriptInstanceHandle() {
  ScriptInstanceHandle handle = _nextScriptInstanceHandle;
  _nextScriptInstanceHandle.id++;
  return handle;
}

void ScriptManager::queueCollision(EntityId a, EntityId b) {
  std::lock_guard lock(_collisionMutex);
  _collisions.emplace_back(a, b);
}

std::vector<std::pair<EntityId, EntityId>> ScriptManager::takeCollisions() {
  std::lock_guard lock(_collisionMutex);
  return std::exchange(_collisions, {});
}

const LoadedScript* ScriptManager::getScript(AssetHandle scriptAsset) const {
  return _scripts.get(scriptAsset);
}
