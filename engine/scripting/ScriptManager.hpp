#pragma once
#include <wasm3.h>

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

#include "LoadedScript.hpp"
#include "ScriptHandle.hpp"
#include "ScriptInstanceHandle.hpp"

class ScriptInstance;

class ScriptManager {
 public:
  ScriptManager();
  ~ScriptManager();

  ScriptHandle loadScript(const std::vector<uint8_t>& wasmBinary);
  ScriptInstanceHandle createInstance(ScriptHandle handle);
  void updateInstance(ScriptInstanceHandle& handle, float dt);
  ScriptInstance* getInstance(ScriptInstanceHandle handle);
  void destroyInstance(ScriptInstanceHandle handle);

 private:
  std::unordered_map<ScriptHandle, LoadedScript> _scripts;
  std::unordered_map<ScriptInstanceHandle, ScriptInstance> _instances;
  ScriptHandle _nextScriptHandle = ScriptHandle{1};
  ScriptInstanceHandle _nextScriptInstanceHandle = ScriptInstanceHandle{1};
  IM3Environment _env = nullptr;

  ScriptHandle generateScriptHandle();
  ScriptInstanceHandle generateScriptInstanceHandle();
};