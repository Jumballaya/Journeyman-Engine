#include "GameHostFunctions.hpp"

#include <wasm3.h>

#include <random>

#include "../logger/logging.hpp"
#include "../scripting/HostFunction.hpp"
#include "../scripting/ScriptManager.hpp"
#include "../scripting/WasmMemory.hpp"
#include "ApplicationEvents.hpp"
#include "Engine.hpp"

// Entity ids cross the wasm boundary either as (index, generation) i32 pairs
// (arguments) or packed into one i64 (return values): generation in the high
// 32 bits, index in the low 32. -1 (all bits set) is "no entity".

namespace {

Engine* s_engine = nullptr;

constexpr int64_t kNoEntity = -1;

int64_t pack(EntityId id) {
  return static_cast<int64_t>((static_cast<uint64_t>(id.generation) << 32) | id.index);
}

EntityId entity(int32_t index, int32_t generation) {
  return EntityId{static_cast<uint32_t>(index), static_cast<uint32_t>(generation)};
}

ScriptInstanceContext* context(IM3Runtime runtime) {
  return static_cast<ScriptInstanceContext*>(m3_GetUserData(runtime));
}

GameState& store(int32_t which) {
  return which == 1 ? s_engine->getSaveState() : s_engine->getSessionState();
}

bool liveEntity(EntityId id) {
  World& world = s_engine->getWorld();
  return world.isAlive(id) && !world.isPendingDestroy(id);
}

}  // namespace

// ---- Entities -------------------------------------------------------------

m3ApiRawFunction(jmSelf) {
  m3ApiReturnType(int64_t);
  auto* ctx = context(runtime);
  m3ApiReturn(ctx ? pack(ctx->eid) : kNoEntity);
}

m3ApiRawFunction(jmEntityIsAlive) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, index);
  m3ApiGetArg(int32_t, generation);
  m3ApiReturn(s_engine && liveEntity(entity(index, generation)) ? 1 : 0);
}

m3ApiRawFunction(jmEntityHasTag) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, index);
  m3ApiGetArg(int32_t, generation);
  m3ApiGetArg(int32_t, tagPtr);
  m3ApiGetArg(int32_t, tagLen);
  auto tag = wasm_memory::readString(runtime, tagPtr, tagLen);
  if (!s_engine || !tag) m3ApiReturn(0);
  m3ApiReturn(s_engine->getWorld().hasTag(entity(index, generation), *tag) ? 1 : 0);
}

m3ApiRawFunction(jmEntitySetTag) {
  m3ApiGetArg(int32_t, index);
  m3ApiGetArg(int32_t, generation);
  m3ApiGetArg(int32_t, tagPtr);
  m3ApiGetArg(int32_t, tagLen);
  m3ApiGetArg(int32_t, present);
  auto tag = wasm_memory::readString(runtime, tagPtr, tagLen);
  if (!s_engine || !tag) m3ApiSuccess();
  World& world = s_engine->getWorld();
  if (present) {
    world.addTag(entity(index, generation), *tag);
  } else {
    world.removeTag(entity(index, generation), *tag);
  }
  m3ApiSuccess();
}

// ---- World queries / spawning ----------------------------------------------

m3ApiRawFunction(jmWorldFindFirst) {
  m3ApiReturnType(int64_t);
  m3ApiGetArg(int32_t, tagPtr);
  m3ApiGetArg(int32_t, tagLen);
  auto tag = wasm_memory::readString(runtime, tagPtr, tagLen);
  if (!s_engine || !tag) m3ApiReturn(kNoEntity);
  for (EntityId id : s_engine->getWorld().findWithTag(*tag)) {
    if (liveEntity(id)) m3ApiReturn(pack(id));
  }
  m3ApiReturn(kNoEntity);
}

// Writes up to `capacity` (index, generation) u32 pairs to outPtr and returns
// the total number of live entities with the tag.
m3ApiRawFunction(jmWorldFindAll) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, tagPtr);
  m3ApiGetArg(int32_t, tagLen);
  m3ApiGetArg(int32_t, outPtr);
  m3ApiGetArg(int32_t, capacity);
  auto tag = wasm_memory::readString(runtime, tagPtr, tagLen);
  if (!s_engine || !tag || capacity < 0) m3ApiReturn(0);
  auto* out = reinterpret_cast<uint32_t*>(wasm_memory::span(runtime, outPtr, capacity * 8));
  int32_t count = 0;
  for (EntityId id : s_engine->getWorld().findWithTag(*tag)) {
    if (!liveEntity(id)) continue;
    if (out && count < capacity) {
      out[count * 2] = id.index;
      out[count * 2 + 1] = id.generation;
    }
    ++count;
  }
  m3ApiReturn(count);
}

m3ApiRawFunction(jmWorldSpawn) {
  m3ApiReturnType(int64_t);
  m3ApiGetArg(int32_t, pathPtr);
  m3ApiGetArg(int32_t, pathLen);
  m3ApiGetArg(float, x);
  m3ApiGetArg(float, y);
  m3ApiGetArg(int32_t, overridesPtr);
  m3ApiGetArg(int32_t, overridesLen);
  auto path = wasm_memory::readString(runtime, pathPtr, pathLen);
  auto overridesText = wasm_memory::readString(runtime, overridesPtr, overridesLen);
  if (!s_engine || !path) m3ApiReturn(kNoEntity);

  nlohmann::json overrides = nlohmann::json::object();
  if (overridesText && !overridesText->empty()) {
    overrides = nlohmann::json::parse(*overridesText, nullptr, false);
    if (overrides.is_discarded() || !overrides.is_object()) {
      JM_LOG_ERROR("[World.spawn] overrides for '{}' are not a JSON object: {}", *path, *overridesText);
      overrides = nlohmann::json::object();
    }
  }
  m3ApiReturn(pack(s_engine->getSpawner().spawn(*path, x, y, std::move(overrides))));
}

m3ApiRawFunction(jmWorldDestroy) {
  m3ApiGetArg(int32_t, index);
  m3ApiGetArg(int32_t, generation);
  if (s_engine) s_engine->getWorld().destroyDeferred(entity(index, generation));
  m3ApiSuccess();
}

// ---- Script params ----------------------------------------------------------

m3ApiRawFunction(jmScriptParamNumber) {
  m3ApiReturnType(double);
  m3ApiGetArg(int32_t, keyPtr);
  m3ApiGetArg(int32_t, keyLen);
  m3ApiGetArg(double, fallback);
  auto key = wasm_memory::readString(runtime, keyPtr, keyLen);
  auto* ctx = context(runtime);
  if (!key || !ctx) m3ApiReturn(fallback);
  auto it = ctx->params.find(*key);
  if (it != ctx->params.end() && it->is_number()) m3ApiReturn(it->get<double>());
  if (it != ctx->params.end() && it->is_boolean()) m3ApiReturn(it->get<bool>() ? 1.0 : 0.0);
  m3ApiReturn(fallback);
}

// Returns the byte length of the string param (copying up to `capacity`), or
// -1 when the param is absent / not a string.
m3ApiRawFunction(jmScriptParamString) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, keyPtr);
  m3ApiGetArg(int32_t, keyLen);
  m3ApiGetArg(int32_t, outPtr);
  m3ApiGetArg(int32_t, capacity);
  auto key = wasm_memory::readString(runtime, keyPtr, keyLen);
  auto* ctx = context(runtime);
  if (!key || !ctx) m3ApiReturn(-1);
  auto it = ctx->params.find(*key);
  if (it == ctx->params.end() || !it->is_string()) m3ApiReturn(-1);
  m3ApiReturn(wasm_memory::writeString(runtime, outPtr, capacity, it->get<std::string>()));
}

// AssemblyScript's Math.random() seeds itself from this import.
m3ApiRawFunction(jmSeed) {
  m3ApiReturnType(double);
  static std::random_device device;
  m3ApiReturn(static_cast<double>(device()) * 4294967296.0 + static_cast<double>(device()));
}

// ---- Time --------------------------------------------------------------------

m3ApiRawFunction(jmTimeScale) {
  m3ApiReturnType(float);
  m3ApiReturn(s_engine ? s_engine->getClock().scale() : 1.0f);
}

m3ApiRawFunction(jmTimeSetScale) {
  m3ApiGetArg(float, scale);
  if (s_engine) s_engine->getClock().setScale(scale);
  m3ApiSuccess();
}

m3ApiRawFunction(jmTimeElapsed) {
  m3ApiReturnType(double);
  m3ApiReturn(s_engine ? s_engine->getClock().elapsed() : 0.0);
}

m3ApiRawFunction(jmTimeUnscaledElapsed) {
  m3ApiReturnType(double);
  m3ApiReturn(s_engine ? s_engine->getClock().unscaledElapsed() : 0.0);
}

m3ApiRawFunction(jmTimeUnscaledDelta) {
  m3ApiReturnType(float);
  m3ApiReturn(s_engine ? s_engine->getClock().unscaledDt() : 0.0f);
}

// ---- Game state (store 0 = session, 1 = persistent save) ---------------------

m3ApiRawFunction(jmStateSetNumber) {
  m3ApiGetArg(int32_t, which);
  m3ApiGetArg(int32_t, keyPtr);
  m3ApiGetArg(int32_t, keyLen);
  m3ApiGetArg(double, value);
  auto key = wasm_memory::readString(runtime, keyPtr, keyLen);
  if (s_engine && key) store(which).setNumber(*key, value);
  m3ApiSuccess();
}

m3ApiRawFunction(jmStateGetNumber) {
  m3ApiReturnType(double);
  m3ApiGetArg(int32_t, which);
  m3ApiGetArg(int32_t, keyPtr);
  m3ApiGetArg(int32_t, keyLen);
  m3ApiGetArg(double, fallback);
  auto key = wasm_memory::readString(runtime, keyPtr, keyLen);
  if (!s_engine || !key) m3ApiReturn(fallback);
  m3ApiReturn(store(which).getNumber(*key, fallback));
}

m3ApiRawFunction(jmStateSetString) {
  m3ApiGetArg(int32_t, which);
  m3ApiGetArg(int32_t, keyPtr);
  m3ApiGetArg(int32_t, keyLen);
  m3ApiGetArg(int32_t, valuePtr);
  m3ApiGetArg(int32_t, valueLen);
  auto key = wasm_memory::readString(runtime, keyPtr, keyLen);
  auto value = wasm_memory::readString(runtime, valuePtr, valueLen);
  if (s_engine && key && value) store(which).setString(*key, std::move(*value));
  m3ApiSuccess();
}

m3ApiRawFunction(jmStateGetString) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, which);
  m3ApiGetArg(int32_t, keyPtr);
  m3ApiGetArg(int32_t, keyLen);
  m3ApiGetArg(int32_t, outPtr);
  m3ApiGetArg(int32_t, capacity);
  auto key = wasm_memory::readString(runtime, keyPtr, keyLen);
  if (!s_engine || !key) m3ApiReturn(-1);
  auto value = store(which).getString(*key);
  if (!value) m3ApiReturn(-1);
  m3ApiReturn(wasm_memory::writeString(runtime, outPtr, capacity, *value));
}

m3ApiRawFunction(jmStateHas) {
  m3ApiReturnType(int32_t);
  m3ApiGetArg(int32_t, which);
  m3ApiGetArg(int32_t, keyPtr);
  m3ApiGetArg(int32_t, keyLen);
  auto key = wasm_memory::readString(runtime, keyPtr, keyLen);
  m3ApiReturn(s_engine && key && store(which).has(*key) ? 1 : 0);
}

m3ApiRawFunction(jmStateRemove) {
  m3ApiGetArg(int32_t, which);
  m3ApiGetArg(int32_t, keyPtr);
  m3ApiGetArg(int32_t, keyLen);
  auto key = wasm_memory::readString(runtime, keyPtr, keyLen);
  if (s_engine && key) store(which).remove(*key);
  m3ApiSuccess();
}

m3ApiRawFunction(jmStateClear) {
  m3ApiGetArg(int32_t, which);
  if (s_engine) store(which).clear();
  m3ApiSuccess();
}

// ---- App -----------------------------------------------------------------------

m3ApiRawFunction(jmAppQuit) {
  if (s_engine) s_engine->getEventBus().emit(EVT_AppQuit, events::Quit{});
  m3ApiSuccess();
}

void registerGameHostFunctions(Engine& engine, ScriptManager& scripts) {
  s_engine = &engine;
  const HostFunction functions[] = {
      {"env", "__jmSelf", "I()", &jmSelf},
      {"env", "__jmEntityIsAlive", "i(ii)", &jmEntityIsAlive},
      {"env", "__jmEntityHasTag", "i(iiii)", &jmEntityHasTag},
      {"env", "__jmEntitySetTag", "v(iiiii)", &jmEntitySetTag},
      {"env", "__jmWorldFindFirst", "I(ii)", &jmWorldFindFirst},
      {"env", "__jmWorldFindAll", "i(iiii)", &jmWorldFindAll},
      {"env", "__jmWorldSpawn", "I(iiffii)", &jmWorldSpawn},
      {"env", "__jmWorldDestroy", "v(ii)", &jmWorldDestroy},
      {"env", "__jmScriptParamNumber", "F(iiF)", &jmScriptParamNumber},
      {"env", "__jmScriptParamString", "i(iiii)", &jmScriptParamString},
      {"env", "seed", "F()", &jmSeed},
      {"env", "__jmTimeScale", "f()", &jmTimeScale},
      {"env", "__jmTimeSetScale", "v(f)", &jmTimeSetScale},
      {"env", "__jmTimeElapsed", "F()", &jmTimeElapsed},
      {"env", "__jmTimeUnscaledElapsed", "F()", &jmTimeUnscaledElapsed},
      {"env", "__jmTimeUnscaledDelta", "f()", &jmTimeUnscaledDelta},
      {"env", "__jmStateSetNumber", "v(iiiF)", &jmStateSetNumber},
      {"env", "__jmStateGetNumber", "F(iiiF)", &jmStateGetNumber},
      {"env", "__jmStateSetString", "v(iiiii)", &jmStateSetString},
      {"env", "__jmStateGetString", "i(iiiii)", &jmStateGetString},
      {"env", "__jmStateHas", "i(iii)", &jmStateHas},
      {"env", "__jmStateRemove", "v(iii)", &jmStateRemove},
      {"env", "__jmStateClear", "v(i)", &jmStateClear},
      {"env", "__jmAppQuit", "v()", &jmAppQuit},
  };
  for (const auto& fn : functions) {
    scripts.registerHostFunction(fn.name, fn);
  }
}

void clearGameHostFunctions() {
  s_engine = nullptr;
}
