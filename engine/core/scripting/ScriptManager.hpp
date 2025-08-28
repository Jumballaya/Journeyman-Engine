#pragma once
#include <wasm3.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "LoadedScript.hpp"
#include "ScriptHandle.hpp"
#include "ScriptInstance.hpp"
#include "ScriptInstanceHandle.hpp"

class Application;

class ScriptManager {
 public:
  ScriptManager();
  ~ScriptManager();

  void initialize(Application& app);

  ScriptHandle loadScript(const std::vector<uint8_t>& wasmBinary, std::vector<std::string>& imports);
  ScriptInstanceHandle createInstance(ScriptHandle handle);
  void updateInstance(ScriptInstanceHandle& handle, float dt);
  ScriptInstance* getInstance(ScriptInstanceHandle handle);
  void destroyInstance(ScriptInstanceHandle handle);
  void registerHostFunction(const std::string& name, const HostFunction& hostFunction);

  LoadedScript* getScript(ScriptHandle handle);

 private:
  std::unordered_map<ScriptHandle, LoadedScript> _scripts;
  std::unordered_map<ScriptInstanceHandle, ScriptInstance> _instances;
  std::unordered_map<std::string, HostFunction> _hostFunctions;
  ScriptHandle _nextScriptHandle = ScriptHandle{1};
  ScriptInstanceHandle _nextScriptInstanceHandle = ScriptInstanceHandle{1};
  IM3Environment _env = nullptr;

  ScriptHandle generateScriptHandle();
  ScriptInstanceHandle generateScriptInstanceHandle();
};