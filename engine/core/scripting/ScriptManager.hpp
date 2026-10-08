#pragma once
#include <wasm3.h>

#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
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

  // Checks the wasm module parses; reloading the same asset replaces it.
  void loadScript(AssetHandle scriptAsset, const std::vector<uint8_t>& wasmBinary, std::string path = {});
  const LoadedScript* getScript(AssetHandle scriptAsset) const { return _scripts.get(scriptAsset); }

  // Instantiates a loaded script for `eid` with its ScriptComponent params.
  // Invalid handle (logged) if the script isn't loaded or fails to start.
  ScriptInstanceHandle createInstance(AssetHandle scriptAsset, EntityId eid,
                                      nlohmann::json params = nlohmann::json::object());

  ScriptInstance* getInstance(ScriptInstanceHandle handle);

  // A server build has no window, renderer, UI or audio: scripts' calls to
  // those host functions do nothing (and read 0) instead of trapping.
  void setStubMissingImports(bool on) { _stubMissing = on; }
  // The host functions scripts have called that were stubbed this way.
  const std::set<std::string>& stubbedImports() const { return _stubbed; }
  void destroyInstance(ScriptInstanceHandle handle) { _instances.erase(handle); }
  size_t instanceCount() const { return _instances.size(); }

  // Exposes `fn` to scripts as env.<name>; see HostBinding.hpp for how C++
  // parameter/return types map to wasm. Rebinding a name replaces it.
  template <typename F>
  void bind(const std::string& name, F fn) {
    _hostFunctions[name] = std::make_unique<host::BoundFunction<F>>(std::move(fn));
  }

  // Every bound function's wasm signature by name, e.g. "v(iiffffii)": what
  // scripts' env imports must match (`journeyman_engine --schema` lists them).
  std::map<std::string, std::string> signatures() const {
    std::map<std::string, std::string> out;
    for (const auto& [name, binding] : _hostFunctions) out[name] = binding->signature();
    return out;
  }

  // Contacts reported by physics; ScriptSystem delivers them as
  // onCollide calls at the start of its next update.
  void queueCollision(EntityId a, EntityId b);
  std::vector<std::pair<EntityId, EntityId>> takeCollisions();

  // Messages between scripts; ScriptSystem delivers them as
  // onMessage calls before the receiver's next update.
  void queueMessage(EntityId to, ScriptMessage message);
  std::vector<std::pair<EntityId, ScriptMessage>> takeMessages();

 private:
  struct FreeEnvironment {
    void operator()(IM3Environment env) const { m3_FreeEnvironment(env); }
  };

  // Declared first so it is freed last: freeing a runtime hands its code pages back to it.
  std::unique_ptr<std::remove_pointer_t<IM3Environment>, FreeEnvironment> _env;
  AssetRegistry<LoadedScript> _scripts;
  std::unordered_map<ScriptInstanceHandle, ScriptInstance> _instances;
  HostBindings _hostFunctions;
  uint32_t _nextInstanceId = 1;
  bool _stubMissing = false;
  std::set<std::string> _stubbed;

  std::vector<std::pair<EntityId, EntityId>> _collisions;
  std::vector<std::pair<EntityId, ScriptMessage>> _messages;
};
