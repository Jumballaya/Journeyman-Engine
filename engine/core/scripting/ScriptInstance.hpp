#pragma once
#include <wasm3.h>

#include <memory>
#include <set>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>

#include "../ecs/entity/EntityId.hpp"
#include "HostBinding.hpp"
#include "ScriptEvent.hpp"
#include "ScriptContext.hpp"

using HostBindings = std::unordered_map<std::string, std::unique_ptr<host::Binding>>;

// One script running for one entity: a wasm3 runtime owning its own parsed module.
// Pinned in place (ScriptManager's map nodes don't move).
class ScriptInstance {
 public:
  // Takes `module` and runs the script's top-level code; throws (freeing
  // everything) if it can't start.
  // `stubbed` non-null: env imports nothing binds (the presentation functions
  // a server build leaves out) become no-ops returning 0, and their names are
  // added to it. Otherwise calling one traps.
  ScriptInstance(std::string scriptPath, EntityId eid, IM3Environment env, IM3Module module,
                 const HostBindings& hostFunctions, nlohmann::json params = nlohmann::json::object(),
                 std::set<std::string>* stubbed = nullptr);

  ScriptInstance(const ScriptInstance&) = delete;
  ScriptInstance& operator=(const ScriptInstance&) = delete;

  // A wasm trap is logged once and disables the instance (later calls do nothing).
  void update(float dt);
  void onEvent(ScriptEvent event, EntityId other);
  void onDestroy();
  void onMessage(const ScriptMessage& message);
  const nlohmann::json& params() const { return _context.params; }
  bool failed() const { return _failed; }
  const std::string& scriptPath() const { return _context.script; }

 private:
  struct FreeRuntime {
    void operator()(IM3Runtime runtime) const { m3_FreeRuntime(runtime); }
  };

  template <typename... Args>
  void call(IM3Function fn, const char* entryPoint, Args... args);

  ScriptInstanceContext _context;
  std::unique_ptr<std::remove_pointer_t<IM3Runtime>, FreeRuntime> _runtime;
  IM3Function _onUpdate = nullptr;
  std::array<IM3Function, kScriptEventExports.size()> _onEvent{};
  IM3Function _onDestroy = nullptr;
  IM3Function _onMessage = nullptr;
  bool _failed = false;
};
