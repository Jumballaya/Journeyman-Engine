#include "ScriptInstance.hpp"

#include <stdexcept>
#include <string>

#include "../logger/logging.hpp"

ScriptInstance::ScriptInstance(
    ScriptInstanceHandle handle, AssetHandle scriptAsset, std::string scriptPath, EntityId eid,
    IM3Environment env, IM3Module module,
    const HostBindings& hostFunctions,
    nlohmann::json params)
    : _handle(handle), _scriptAsset(scriptAsset) {
  bindEntity(eid);
  _context.script = std::move(scriptPath);
  // Params are visible to top-level script code (which runs in the start
  // function below), not just to onUpdate.
  _context.params = params.is_object() ? std::move(params) : nlohmann::json::object();

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
    JM_LOG_ERROR("[Script] {} can't load into a runtime: {}", _context.script, result);
    throw std::runtime_error(std::string("unable to load wasm module into runtime: ") + result);
  }
  // From here on, `module` is owned by `_runtime`. m3_FreeRuntime in the
  // destructor (or on the failure paths below) releases both.

  // Link every host function; functionLookupFailed just means the script
  // doesn't import it. Anything else (e.g. signature mismatch) is fatal.
  for (const auto& [name, binding] : hostFunctions) {
    M3Result linkResult = m3_LinkRawFunctionEx(module, "env", name.c_str(), binding->signature().c_str(),
                                               binding->thunk(), binding.get());
    if (linkResult == m3Err_none || linkResult == m3Err_functionLookupFailed) {
      continue;
    }
    JM_LOG_ERROR("[Script] {} can't link host function {} {}: {}", _context.script, name, binding->signature(),
                 linkResult);
    m3_FreeRuntime(_runtime);
    _runtime = nullptr;
    throw std::runtime_error(
        "Failed to link host function [" + name +
        "]: " + std::string(linkResult));
  }

  result = m3_RunStart(module);
  if (result != m3Err_none) {
    M3ErrorInfo info;
    m3_GetErrorInfo(_runtime, &info);
    JM_LOG_ERROR("[Script] {} trapped while starting: {}{}{}", _context.script, result,
                 info.message ? ": " : "", info.message ? info.message : "");
    m3_FreeRuntime(_runtime);
    _runtime = nullptr;
    throw std::runtime_error(std::string("script start function trapped: ") + result);
  }

  result = m3_FindFunction(&_onUpdate, _runtime, "onUpdate");
  if (result != m3Err_none) {
    JM_LOG_ERROR("[Script] {} has no onUpdate", _context.script);
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
  if (_failed || !_onUpdate) return;
  M3Result result = m3_CallV(_onUpdate, dt);
  if (result != m3Err_none) fail("onUpdate", result);
}

void ScriptInstance::onCollide(EntityId id) {
  if (_failed || !_onCollide) return;
  M3Result result = m3_CallV(_onCollide, id.index, id.generation);
  if (result != m3Err_none) fail("onCollide", result);
}

void ScriptInstance::fail(const char* entryPoint, M3Result result) {
  _failed = true;
  M3ErrorInfo info;
  m3_GetErrorInfo(_runtime, &info);
  JM_LOG_ERROR("[Script] {} trapped in {} on entity {}:{} ({}{}{}); script disabled", _context.script,
               entryPoint, _context.eid.index, _context.eid.generation, result,
               info.message ? ": " : "", info.message ? info.message : "");
}

void ScriptInstance::bindEntity(EntityId id) {
  _context.eid.index = id.index;
  _context.eid.generation = id.generation;
}
