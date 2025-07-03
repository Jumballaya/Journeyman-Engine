#include "ScriptInstance.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "HostFunctions.hpp"

ScriptInstance::ScriptInstance(ScriptInstanceHandle handle, IM3Environment env,
                               const LoadedScript& script, const std::vector<HostFunction>& hostFunctions)
    : _handle(handle) {
  _runtime = m3_NewRuntime(env, 64 * 1024, nullptr);
  if (!_runtime) {
    throw std::runtime_error("unable to create wasm runtime for a script instance");
  }

  M3Result result = m3_LoadModule(_runtime, script.module);
  if (result != m3Err_none) {
    throw std::runtime_error(std::string("unable to load wasm module into runtime: ") + result);
  }

  for (const auto& host : hostFunctions) {
    std::cout << "[ScriptInstance] Linking host function: " << host.module << "." << host.name << "\n";
    M3Result linkResult = m3_LinkRawFunction(script.module, host.module, host.name, host.signature, host.function);
    if (linkResult != m3Err_none) {
      throw std::runtime_error("Failed to link host function [" + std::string(host.name) + "]: " + std::string(linkResult));
    }
  }

  m3_RunStart(script.module);

  // Get the Lifecycle methods
  result = m3_FindFunction(&_onUpdate, _runtime, "onUpdate");
  if (result != m3Err_none) {
    throw std::runtime_error("onUpdate function not found in script.");
  }
}

void ScriptInstance::update(float dt) {
  if (!_onUpdate) return;

  std::string dtStr = std::to_string(dt);
  const char* argv[2] = {dtStr.c_str(), nullptr};

  M3Result result = m3_CallArgv(_onUpdate, 1, argv);
  if (result != m3Err_none) {
    throw std::runtime_error(std::string("Error calling onUpdate: ") + result);
  }
}