#pragma once
#include <wasm3.h>

#include <string>
#include <unordered_map>

#include "HostFunction.hpp"
#include "LoadedScript.hpp"
#include "ScriptInstanceHandle.hpp"

class ScriptInstance {
 public:
  ScriptInstance(
      ScriptInstanceHandle handle,
      IM3Environment env,
      const LoadedScript& script,
      const std::unordered_map<std::string, HostFunction>& hostFunction);

  void update(float dt);

  ScriptInstanceHandle handle() const { return _handle; }

 private:
  ScriptInstanceHandle _handle;
  IM3Runtime _runtime = nullptr;
  IM3Function _onUpdate = nullptr;
};