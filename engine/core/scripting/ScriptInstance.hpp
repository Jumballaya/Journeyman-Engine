#pragma once
#include <wasm3.h>

#include <string>
#include <unordered_map>

#include "../ecs/entity/EntityId.hpp"
#include "HostFunction.hpp"
#include "LoadedScript.hpp"
#include "ScriptHandle.hpp"
#include "ScriptInstanceHandle.hpp"

struct ScriptInstanceContext {
  EntityId eid;
};

class ScriptInstance {
 public:
  ScriptInstance(
      ScriptInstanceHandle handle,
      ScriptHandle scriptHandle,
      EntityId eid,
      IM3Environment env,
      const LoadedScript& script,
      const std::unordered_map<std::string, HostFunction>& hostFunction);

  void bindEntity(EntityId id);

  void update(float dt);

  ScriptInstanceHandle handle() const { return _handle; }
  ScriptHandle getScriptHandle() const { return _scriptHandle; }

 private:
  ScriptInstanceHandle _handle;
  ScriptHandle _scriptHandle;
  IM3Runtime _runtime = nullptr;
  IM3Function _onUpdate = nullptr;

  ScriptInstanceContext _context;
};