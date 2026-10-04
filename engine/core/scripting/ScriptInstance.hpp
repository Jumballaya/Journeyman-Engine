#pragma once
#include <wasm3.h>

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

#include "../assets/AssetHandle.hpp"
#include "../ecs/entity/EntityId.hpp"
#include "HostFunction.hpp"
#include "ScriptInstanceHandle.hpp"

// Reached from host functions via m3_GetUserData(runtime): identifies the
// entity the running script belongs to plus its authored parameters.
struct ScriptInstanceContext {
  EntityId eid;
  nlohmann::json params = nlohmann::json::object();
};

// Owns a wasm3 runtime created from a freshly-parsed module. The module is
// passed in already parsed; on construction `m3_LoadModule` transfers
// ownership to `_runtime`, so freeing `_runtime` in the destructor frees
// both. ScriptInstance is non-copyable and non-movable: ScriptManager stores
// instances in an unordered_map and constructs them in place via try_emplace
// (node storage is stable across rehash, so neither copy nor move is needed).
class ScriptInstance {
 public:
  ScriptInstance(
      ScriptInstanceHandle handle,
      AssetHandle scriptAsset,
      EntityId eid,
      IM3Environment env,
      IM3Module module,
      const std::unordered_map<std::string, HostFunction>& hostFunctions,
      nlohmann::json params = nlohmann::json::object());
  ~ScriptInstance();

  ScriptInstance(const ScriptInstance&) = delete;
  ScriptInstance& operator=(const ScriptInstance&) = delete;
  ScriptInstance(ScriptInstance&&) = delete;
  ScriptInstance& operator=(ScriptInstance&&) = delete;

  void bindEntity(EntityId id);

  // Script entry points. A wasm trap is logged once and the instance is
  // disabled (later calls are no-ops) so one broken script can't stall the
  // rest of the frame or flood the log.
  void update(float dt);
  void onCollide(EntityId id);
  bool failed() const { return _failed; }

  ScriptInstanceHandle handle() const { return _handle; }
  AssetHandle getScriptAsset() const { return _scriptAsset; }

 private:
  ScriptInstanceHandle _handle;
  AssetHandle _scriptAsset;
  IM3Runtime _runtime = nullptr;
  IM3Function _onUpdate = nullptr;
  IM3Function _onCollide = nullptr;
  bool _failed = false;

  void fail(const char* entryPoint, M3Result result);

  ScriptInstanceContext _context;
};
