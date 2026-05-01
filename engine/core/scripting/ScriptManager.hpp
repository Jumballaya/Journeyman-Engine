#pragma once
#include <wasm3.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "../assets/AssetHandle.hpp"
#include "../assets/AssetRegistry.hpp"
#include "../ecs/entity/EntityId.hpp"
#include "LoadedScript.hpp"
#include "ScriptInstance.hpp"
#include "ScriptInstanceHandle.hpp"

class Engine;

// ScriptManager owns parsed wasm modules (LoadedScript) and per-entity
// instances. LoadedScript is indexed by AssetHandle — the same handle the
// AssetManager issues for the `.script.json` the script came from. Scripts
// are always loaded from disk in this engine; there's no runtime synthesis,
// so AssetHandle is the natural identity and a separate ScriptHandle type
// would be redundant.
//
// ScriptInstance stays keyed by ScriptInstanceHandle — instances are
// per-entity, not per-asset.
class ScriptManager {
 public:
  ScriptManager();
  ~ScriptManager();

  void initialize(Engine& app);

  // Parse a wasm module and cache it under `scriptAsset`. Re-loading the same
  // AssetHandle replaces the existing LoadedScript (hot-reload semantics from
  // AssetRegistry::insert).
  void loadScript(AssetHandle scriptAsset,
                  const std::vector<uint8_t>& wasmBinary);

  // Create a per-entity instance of the script previously cached for
  // `scriptAsset`. Returns a zero-valued (invalid) handle if the asset hasn't
  // been loaded — callers must check isValid() and decide their fallback.
  ScriptInstanceHandle createInstance(AssetHandle scriptAsset, EntityId eid);

  void updateInstance(ScriptInstanceHandle& handle, float dt);
  ScriptInstance* getInstance(ScriptInstanceHandle handle);
  void destroyInstance(ScriptInstanceHandle handle);
  void registerHostFunction(const std::string& name, const HostFunction& hostFunction);

  size_t instanceCount() const { return _instances.size(); }

  const LoadedScript* getScript(AssetHandle scriptAsset) const;

 private:
  AssetRegistry<LoadedScript> _scripts;
  std::unordered_map<ScriptInstanceHandle, ScriptInstance> _instances;
  std::unordered_map<std::string, HostFunction> _hostFunctions;
  ScriptInstanceHandle _nextScriptInstanceHandle = ScriptInstanceHandle{1};
  IM3Environment _env = nullptr;

  ScriptInstanceHandle generateScriptInstanceHandle();
};
