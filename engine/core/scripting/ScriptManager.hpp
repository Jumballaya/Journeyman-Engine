#pragma once
#include <wasm3.h>

#include <mutex>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

#include "../assets/AssetHandle.hpp"
#include "../assets/AssetRegistry.hpp"
#include "../ecs/entity/EntityId.hpp"
#include "LoadedScript.hpp"
#include "ScriptInstance.hpp"
#include "ScriptInstanceHandle.hpp"

// Owns compiled scripts (one per script asset) and the per-entity instances
// running them, plus the host functions every instance is linked against.
class ScriptManager {
 public:
  ScriptManager();
  ~ScriptManager();

  // Parses a wasm module; reloading the same asset replaces it.
  void loadScript(AssetHandle scriptAsset, const std::vector<uint8_t>& wasmBinary, std::string path = {});

  // Instantiates a loaded script for `eid` with its ScriptComponent params.
  // Invalid handle (logged) if the script isn't loaded or fails to start.
  ScriptInstanceHandle createInstance(AssetHandle scriptAsset, EntityId eid,
                                      nlohmann::json params = nlohmann::json::object());

  ScriptInstance* getInstance(ScriptInstanceHandle handle);
  void destroyInstance(ScriptInstanceHandle handle);
  // Exposes `fn` to scripts as env.<name>; see HostBinding.hpp for how C++
  // parameter/return types map to wasm. Rebinding a name replaces it.
  template <typename F>
  void bind(const std::string& name, F fn) {
    _hostFunctions[name] = std::make_unique<host::BoundFunction<F>>(std::move(fn));
  }

  size_t instanceCount() const { return _instances.size(); }

  // Contacts reported by physics (any thread); ScriptSystem delivers them as
  // onCollide calls at the start of its next update.
  void queueCollision(EntityId a, EntityId b);
  std::vector<std::pair<EntityId, EntityId>> takeCollisions();

  const LoadedScript* getScript(AssetHandle scriptAsset) const;

 private:
  AssetRegistry<LoadedScript> _scripts;
  std::unordered_map<ScriptInstanceHandle, ScriptInstance> _instances;
  HostBindings _hostFunctions;
  ScriptInstanceHandle _nextScriptInstanceHandle = ScriptInstanceHandle{1};
  IM3Environment _env = nullptr;
  std::mutex _collisionMutex;
  std::vector<std::pair<EntityId, EntityId>> _collisions;

  ScriptInstanceHandle generateScriptInstanceHandle();
};
