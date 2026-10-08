#include "ScriptManager.hpp"

#include "Fuel.hpp"

#include <stdexcept>
#include <string>

#include "../logger/logging.hpp"

ScriptManager::ScriptManager() : _env(m3_NewEnvironment()) {
  fuel::install();
  if (!_env) throw std::runtime_error("unable to create wasm3 environment");
}

void ScriptManager::loadScript(AssetHandle scriptAsset, const std::vector<uint8_t>& wasmBinary, std::string path) {
  // Parse now only to report errors at load time: a wasm3 module binds to one
  // runtime, so each instance parses its own copy.
  IM3Module module = nullptr;
  M3Result result = m3_ParseModule(_env.get(), &module, wasmBinary.data(), wasmBinary.size());
  if (result != m3Err_none) throw std::runtime_error(std::string("Failed to parse wasm module: ") + result);
  m3_FreeModule(module);
  _scripts.insert(scriptAsset, LoadedScript{std::move(path), wasmBinary});
}

ScriptInstanceHandle ScriptManager::createInstance(AssetHandle scriptAsset, EntityId eid, nlohmann::json params) {
  const LoadedScript* script = _scripts.get(scriptAsset);
  if (!script) {
    JM_LOG_ERROR("[ScriptManager] createInstance: no script loaded for asset id {}", scriptAsset.id);
    return {};
  }

  IM3Module module = nullptr;
  M3Result parsed = m3_ParseModule(_env.get(), &module, script->binary.data(), script->binary.size());
  if (parsed != m3Err_none) {
    JM_LOG_ERROR("[ScriptManager] createInstance: parse failed for asset id {}: {}", scriptAsset.id, parsed);
    return {};
  }

  const ScriptInstanceHandle handle{_nextInstanceId++};
  try {
    _instances.try_emplace(handle, script->path, eid, _env.get(), module, _hostFunctions, std::move(params));
  } catch (const std::exception& e) {
    JM_LOG_ERROR("[Script] {} failed to start: {}", script->path, e.what());
    return {};
  }
  return handle;
}

ScriptInstance* ScriptManager::getInstance(ScriptInstanceHandle handle) {
  auto it = _instances.find(handle);
  return it == _instances.end() ? nullptr : &it->second;
}

void ScriptManager::queueCollision(EntityId a, EntityId b) {
  _collisions.emplace_back(a, b);
}

std::vector<std::pair<EntityId, EntityId>> ScriptManager::takeCollisions() {
  return std::exchange(_collisions, {});
}

void ScriptManager::queueMessage(EntityId to, ScriptMessage message) {
  _messages.emplace_back(to, std::move(message));
}

std::vector<std::pair<EntityId, ScriptMessage>> ScriptManager::takeMessages() {
  return std::exchange(_messages, {});
}
