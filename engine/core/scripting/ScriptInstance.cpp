#include "ScriptInstance.hpp"

#include <stdexcept>
#include <string>

#include "../logger/logging.hpp"

namespace {

// wasm3's result plus the runtime's detail message, if any.
std::string describe(IM3Runtime runtime, M3Result result) {
  M3ErrorInfo info{};
  m3_GetErrorInfo(runtime, &info);
  return info.message ? std::string(result) + ": " + info.message : std::string(result);
}

IM3Function exported(IM3Runtime runtime, const char* name) {
  IM3Function fn = nullptr;
  return m3_FindFunction(&fn, runtime, name) == m3Err_none ? fn : nullptr;
}

}  // namespace

ScriptInstance::ScriptInstance(std::string scriptPath, EntityId eid, IM3Environment env, IM3Module module,
                               const HostBindings& hostFunctions, nlohmann::json params) {
  _context.eid = eid;
  _context.script = std::move(scriptPath);
  // Params are visible to top-level script code (which runs in the start
  // function below), not just to onUpdate.
  _context.params = params.is_object() ? std::move(params) : nlohmann::json::object();

  _runtime.reset(m3_NewRuntime(env, 64 * 1024, &_context));
  const M3Result loaded = _runtime ? m3_LoadModule(_runtime.get(), module) : "can't create a wasm runtime";
  if (loaded != m3Err_none) {
    m3_FreeModule(module);  // still ours: a failed m3_LoadModule doesn't take it
    throw std::runtime_error(std::string("can't load into a runtime: ") + loaded);
  }
  // From here on `module` belongs to the runtime, which frees it.

  // functionLookupFailed just means the script doesn't import that host
  // function; anything else (e.g. a signature mismatch) is fatal.
  for (const auto& [name, binding] : hostFunctions) {
    const M3Result linked = m3_LinkRawFunctionEx(module, "env", name.c_str(), binding->signature().c_str(),
                                                 binding->thunk(), binding.get());
    if (linked != m3Err_none && linked != m3Err_functionLookupFailed) {
      throw std::runtime_error("can't link host function " + name + " " + binding->signature() + ": " + linked);
    }
  }

  if (const M3Result started = m3_RunStart(module); started != m3Err_none) {
    throw std::runtime_error("trapped while starting: " + describe(_runtime.get(), started));
  }
  _onUpdate = exported(_runtime.get(), "onUpdate");
  if (!_onUpdate) throw std::runtime_error("has no onUpdate");
  _onCollide = exported(_runtime.get(), "onCollide");
  // jm build's entry wrapper exports it; it pulls the message through host calls.
  _onMessage = exported(_runtime.get(), "__jmOnMessage");
}

template <typename... Args>
void ScriptInstance::call(IM3Function fn, const char* entryPoint, Args... args) {
  if (_failed || !fn) return;
  const M3Result result = m3_CallV(fn, args...);
  if (result == m3Err_none) return;
  _failed = true;
  JM_LOG_ERROR("[Script] {} trapped in {} on entity {}:{} ({}); script disabled", _context.script, entryPoint,
               _context.eid.index, _context.eid.generation, describe(_runtime.get(), result));
}

void ScriptInstance::update(float dt) { call(_onUpdate, "onUpdate", dt); }

void ScriptInstance::onCollide(EntityId id) { call(_onCollide, "onCollide", id.index, id.generation); }

void ScriptInstance::onMessage(const ScriptMessage& message) {
  _context.message = &message;
  call(_onMessage, "onMessage");
  _context.message = nullptr;
}
