#pragma once
#include <wasm3.h>

#include <string>
#include <unordered_map>

#include "../assets/AssetHandle.hpp"
#include "../ecs/entity/EntityId.hpp"
#include "HostFunction.hpp"
#include "LoadedScript.hpp"
#include "ScriptInstanceHandle.hpp"

struct ScriptInstanceContext {
  EntityId eid;
};

class ScriptInstance {
 public:
  ScriptInstance(
      ScriptInstanceHandle handle,
      AssetHandle scriptAsset,
      EntityId eid,
      IM3Environment env,
      const LoadedScript& script,
      const std::unordered_map<std::string, HostFunction>& hostFunction);

  void bindEntity(EntityId id);

  void update(float dt);
  void onCollide(EntityId id);

  ScriptInstanceHandle handle() const { return _handle; }
  AssetHandle getScriptAsset() const { return _scriptAsset; }

 private:
  ScriptInstanceHandle _handle;
  AssetHandle _scriptAsset;
  IM3Runtime _runtime = nullptr;
  IM3Function _onUpdate = nullptr;
  IM3Function _onCollide = nullptr;

  ScriptInstanceContext _context;
};