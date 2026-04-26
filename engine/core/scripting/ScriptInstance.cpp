#include "ScriptInstance.hpp"

#include <stdexcept>
#include <string>

#include "../logger/logging.hpp"
#include "HostFunction.hpp"

ScriptInstance::ScriptInstance(
    ScriptInstanceHandle handle, AssetHandle scriptAsset, EntityId eid,
    IM3Environment env, IM3Module module,
    const std::vector<std::string>& imports,
    const std::unordered_map<std::string, HostFunction>& hostFunctions)
    : _handle(handle), _scriptAsset(scriptAsset) {
  bindEntity(eid);

  _runtime = m3_NewRuntime(env, 64 * 1024, &_context);
  if (!_runtime) {
    // Module ownership is still ours since LoadModule never ran.
    m3_FreeModule(module);
    throw std::runtime_error("unable to create wasm runtime for a script instance");
  }

  M3Result result = m3_LoadModule(_runtime, module);
  if (result != m3Err_none) {
    // wasm3: a failing m3_LoadModule leaves module ownership with the caller.
    m3_FreeModule(module);
    m3_FreeRuntime(_runtime);
    _runtime = nullptr;
    JM_LOG_ERROR("unable to load wasm module into runtime: {}", result);
    throw std::runtime_error(std::string("unable to load wasm module into runtime: ") + result);
  }
  // From here on, `module` is owned by `_runtime`. m3_FreeRuntime in the
  // destructor (or on the failure paths below) releases both.

  for (const auto& import : imports) {
    const auto& host = hostFunctions.find(import);
    if (host == hostFunctions.end()) {
      continue;
    }
    M3Result linkResult = m3_LinkRawFunction(
        module, host->second.module, host->second.name,
        host->second.signature, host->second.function);
    if (linkResult != m3Err_none) {
      JM_LOG_ERROR("Failed to link host function [{}]: {}",
                   host->second.name, linkResult);
      m3_FreeRuntime(_runtime);
      _runtime = nullptr;
      throw std::runtime_error(
          "Failed to link host function [" + std::string(host->second.name) +
          "]: " + std::string(linkResult));
    }
  }

  m3_RunStart(module);

  result = m3_FindFunction(&_onUpdate, _runtime, "onUpdate");
  if (result != m3Err_none) {
    JM_LOG_ERROR("onUpdate function not found in script");
    m3_FreeRuntime(_runtime);
    _runtime = nullptr;
    throw std::runtime_error("onUpdate function not found in script.");
  }

  result = m3_FindFunction(&_onCollide, _runtime, "onCollide");
  if (result != m3Err_none) {
    _onCollide = nullptr;
  }
}

ScriptInstance::~ScriptInstance() {
  if (_runtime) {
    m3_FreeRuntime(_runtime);
    _runtime = nullptr;
  }
}

void ScriptInstance::update(float dt) {
  if (!_onUpdate) {
    return;
  }

  std::string dtStr = std::to_string(dt);
  const char* argv[2] = {dtStr.c_str(), nullptr};

  M3Result result = m3_CallArgv(_onUpdate, 1, argv);
  if (result != m3Err_none) {
    JM_LOG_ERROR("Error calling onUpdate: {}", result);
    throw std::runtime_error(std::string("Error calling onUpdate: ") + result);
  }
}

void ScriptInstance::onCollide(EntityId id) {
  if (!_onCollide) {
    return;
  }

  std::string idxStr = std::to_string(id.index);
  std::string genStr = std::to_string(id.generation);
  const char* argv[3] = {idxStr.c_str(), genStr.c_str(), nullptr};

  M3Result result = m3_CallArgv(_onCollide, 2, argv);
  if (result != m3Err_none) {
    JM_LOG_ERROR("Error calling onCollide: {}", result);
    throw std::runtime_error(std::string("Error calling onCollide: ") + result);
  }
}

void ScriptInstance::bindEntity(EntityId id) {
  _context.eid.index = id.index;
  _context.eid.generation = id.generation;
}
