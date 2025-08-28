#include "ScriptInstance.hpp"

#include <iostream>
#include <stdexcept>

#include "../logger/logging.hpp"
#include "HostFunction.hpp"

ScriptInstance::ScriptInstance(ScriptInstanceHandle handle, ScriptHandle scriptHandle, IM3Environment env,
                               const LoadedScript& script, const std::unordered_map<std::string, HostFunction>& hostFunctions)
    : _handle(handle), _scriptHandle(scriptHandle) {
  _runtime = m3_NewRuntime(env, 64 * 1024, nullptr);
  if (!_runtime) {
    throw std::runtime_error("unable to create wasm runtime for a script instance");
  }

  M3Result result = m3_LoadModule(_runtime, script.module);
  if (result != m3Err_none) {
    JM_LOG_ERROR("unable to load wasm module into runtime: {}", result);
    throw std::runtime_error(std::string("unable to load wasm module into runtime: ") + result);
  }

  for (const auto& import : script.imports) {
    const auto& host = hostFunctions.find(import);
    if (host == hostFunctions.end()) {
      continue;
    }

    M3Result linkResult = m3_LinkRawFunction(script.module, host->second.module, host->second.name, host->second.signature, host->second.function);
    if (linkResult != m3Err_none) {
      JM_LOG_ERROR("Failed to link host function [{}]: {}", host->second.name, linkResult);
      throw std::runtime_error("Failed to link host function [" + std::string(host->second.name) + "]: " + std::string(linkResult));
    }
  }

  m3_RunStart(script.module);

  // Get the Lifecycle methods
  result = m3_FindFunction(&_onUpdate, _runtime, "onUpdate");
  if (result != m3Err_none) {
    JM_LOG_ERROR("onUpdate function not found in script");
    throw std::runtime_error("onUpdate function not found in script.");
  }
}

void ScriptInstance::update(float dt) {
  if (!_onUpdate) return;

  std::string dtStr = std::to_string(dt);
  const char* argv[2] = {dtStr.c_str(), nullptr};

  M3Result result = m3_CallArgv(_onUpdate, 1, argv);
  if (result != m3Err_none) {
    JM_LOG_ERROR("Error calling onUpdate: {}", result);
    throw std::runtime_error(std::string("Error calling onUpdate: ") + result);
  }
}