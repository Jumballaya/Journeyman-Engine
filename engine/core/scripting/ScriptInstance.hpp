#pragma once
#include <wasm3.h>

#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

#include "../assets/AssetHandle.hpp"
#include "../ecs/entity/EntityId.hpp"
#include "HostBinding.hpp"
#include "ScriptContext.hpp"
#include "ScriptInstanceHandle.hpp"

using HostBindings = std::unordered_map<std::string, std::unique_ptr<host::Binding>>;

// One script running for one entity: a wasm3 runtime owning its own parsed module.
// Pinned in place (ScriptManager's map nodes don't move).
class ScriptInstance {
 public:
  ScriptInstance(
      ScriptInstanceHandle handle,
      AssetHandle scriptAsset,
      std::string scriptPath,
      EntityId eid,
      IM3Environment env,
      IM3Module module,
      const HostBindings& hostFunctions,
      nlohmann::json params = nlohmann::json::object());
  ~ScriptInstance();

  ScriptInstance(const ScriptInstance&) = delete;
  ScriptInstance& operator=(const ScriptInstance&) = delete;
  ScriptInstance(ScriptInstance&&) = delete;
  ScriptInstance& operator=(ScriptInstance&&) = delete;

  void bindEntity(EntityId id);

  // A wasm trap is logged once and disables the instance (later calls do nothing).
  void update(float dt);
  void onCollide(EntityId id);
  void onMessage(const ScriptMessage& message);
  const nlohmann::json& params() const { return _context.params; }
  bool failed() const { return _failed; }

  ScriptInstanceHandle handle() const { return _handle; }
  AssetHandle getScriptAsset() const { return _scriptAsset; }

 private:
  ScriptInstanceHandle _handle;
  AssetHandle _scriptAsset;
  IM3Runtime _runtime = nullptr;
  IM3Function _onUpdate = nullptr;
  IM3Function _onCollide = nullptr;
  IM3Function _onMessage = nullptr;
  bool _failed = false;

  void fail(const char* entryPoint, M3Result result);

  ScriptInstanceContext _context;
};
