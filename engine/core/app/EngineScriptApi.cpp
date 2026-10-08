// Engine-level script API (entities, fields, spawning, params, time, state,
// scenes); modules bind their own. Script side: cli/internal/stdlib/runtime/.

#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <unordered_map>
#include <vector>

#include "../logger/logging.hpp"
#include "../scripting/ScriptComponent.hpp"
#include "ApplicationEvents.hpp"
#include "Engine.hpp"

using host::AsString;
using host::ScriptCall;
using host::WasmBytes;

namespace {

// What scripts read as "no entity".
constexpr EntityId kNoEntity{UINT32_MAX, UINT32_MAX};

bool alive(World& world, EntityId id) {
  return world.isAlive(id) && !world.isPendingDestroy(id);
}

double paramNumber(const nlohmann::json& params, const std::string& key, double fallback) {
  auto it = params.find(key);
  if (it == params.end()) return fallback;
  if (it->is_number()) return it->get<double>();
  if (it->is_boolean()) return it->get<bool>() ? 1.0 : 0.0;
  return fallback;
}

std::optional<std::string> paramString(const nlohmann::json& params, const std::string& key) {
  auto it = params.find(key);
  if (it == params.end() || !it->is_string()) return std::nullopt;
  return it->get<std::string>();
}

}  // namespace

const nlohmann::json* Engine::paramsOf(EntityId id) {
  auto* script = alive(_world, id) ? _world.getComponent<ScriptComponent>(id) : nullptr;
  if (!script) return nullptr;
  // Starting a script moves its params into the instance.
  if (const ScriptInstance* instance = _scriptManager.getInstance(script->instance)) return &instance->params();
  return &script->params;
}

void Engine::bindScriptApi() {
  ScriptManager& s = _scriptManager;

  // ---- Logging -----------------------------------------------------------------
  s.bind("__jmLog", [this](std::string message) {
    // A driven run's stdout is the driver's replies.
    (_options.dev.drive ? std::cerr : std::cout) << "[script] " << message << "\n";
    JM_LOG_INFO("[script] {}", message);
  });
  // AssemblyScript calls this on a failed assertion / runtime error, then traps.
  s.bind("abort", [](ScriptCall& call, AsString message, AsString file, int32_t line, int32_t column) {
    // asc names files from the scripts package (assets/scripts); "~lib/..." is the standard library.
    const std::string source = file.text.starts_with("~lib/") || file.text.starts_with("assets/")
                                   ? file.text
                                   : (std::filesystem::path("assets/scripts") / file.text).lexically_normal().generic_string();
    JM_REPORT_ERROR((ErrorSource{source, line, column}), "[Script] {} aborted: {} at {}:{}:{}", call.script.script,
                    message.text, source, line, column);
    std::cerr << "[script] " << call.script.script << " aborted: " << message.text << " at " << source << ":"
              << line << ":" << column << "\n";
  });
  // Seeds a script instance's Math.random() (on its first call): the run's next
  // seed, as a double that holds it exactly (53 bits).
  s.bind("seed", [this]() -> double { return static_cast<double>(_seeds.next() >> 11); });

  // ---- Entities & world ----------------------------------------------------------
  s.bind("__jmSelf", [](ScriptCall& call) { return call.self(); });
  s.bind("__jmEntityIsAlive", [this](EntityId id) { return alive(_world, id); });
  s.bind("__jmEntityHasTag", [this](EntityId id, std::string tag) { return _world.hasTag(id, tag); });
  s.bind("__jmEntitySetTag", [this](EntityId id, std::string tag, bool present) {
    present ? _world.addTag(id, tag) : _world.removeTag(id, tag);
  });
  s.bind("__jmWorldDestroy", [this](EntityId id) { _world.destroyDeferred(id); });
  s.bind("__jmWorldFindFirst", [this](std::string tag) {
    for (EntityId id : _world.findWithTag(tag)) {
      if (alive(_world, id)) return id;
    }
    return kNoEntity;
  });
  // Writes (index, generation) u32 pairs while they fit; returns the total count.
  s.bind("__jmWorldFindAll", [this](std::string tag, WasmBytes out) {
    int32_t count = 0;
    for (EntityId id : _world.findWithTag(tag)) {
      if (!alive(_world, id)) continue;
      if (static_cast<size_t>(count + 1) * 8 <= out.size) {
        std::memcpy(out.data + count * 8, &id.index, 4);
        std::memcpy(out.data + count * 8 + 4, &id.generation, 4);
      }
      ++count;
    }
    return count;
  });
  s.bind("__jmEntityParent", [this](EntityId id) { return alive(_world, id) ? _world.parentOf(id) : kNoEntity; });
  // Writes (index, generation) u32 pairs while they fit; returns the total count.
  s.bind("__jmEntityChildren", [this](EntityId id, WasmBytes out) {
    int32_t count = 0;
    for (EntityId child : alive(_world, id) ? _world.childrenOf(id) : std::vector<EntityId>{}) {
      if (!alive(_world, child)) continue;
      if (static_cast<size_t>(count + 1) * 8 <= out.size) {
        std::memcpy(out.data + count * 8, &child.index, 4);
        std::memcpy(out.data + count * 8 + 4, &child.generation, 4);
      }
      ++count;
    }
    return count;
  });
  s.bind("__jmEntityAttach", [this](EntityId child, EntityId parent) { _spawner.attach(child, parent); });
  s.bind("__jmEntityHasComponent", [this](EntityId id, std::string component) {
    return _world.hasComponentNamed(id, component);
  });
  s.bind("__jmWorldSpawn", [this](std::string prefab, float x, float y, std::string overrides) {
    nlohmann::json json = nlohmann::json::object();
    if (!overrides.empty()) {
      json = nlohmann::json::parse(overrides, nullptr, false);
      if (!json.is_object()) {
        JM_LOG_ERROR("[script] spawn '{}': overrides are not a JSON object", prefab);
        json = nlohmann::json::object();
      }
    }
    return _spawner.spawn(_manifest.resolve(prefab, ".prefab.json"), x, y, std::move(json));
  });

  // ---- Component fields (ComponentSpec::scriptFields), by id from __jmFieldId ----------
  // Every script instance looks its fields up at start, so ids are shared.
  auto fields = std::make_shared<std::vector<World::ScriptFieldRef>>();
  auto fieldIds = std::make_shared<std::unordered_map<std::string, int32_t>>();
  s.bind("__jmFieldId", [this, fields, fieldIds](std::string component, std::string field) -> int32_t {
    const std::string key = component + "." + field;
    if (auto it = fieldIds->find(key); it != fieldIds->end()) return it->second;
    auto ref = _world.findScriptField(component, field);
    if (!ref) {
      JM_LOG_ERROR("[script] {} has no script field '{}'", component, field);
      return -1;
    }
    fields->push_back(*ref);
    return (*fieldIds)[key] = static_cast<int32_t>(fields->size() - 1);
  });
  // Raw 4-byte values; reads of a missing component give 0.
  s.bind("__jmFieldGet", [this, fields](EntityId id, int32_t field) -> uint32_t {
    if (field < 0 || static_cast<size_t>(field) >= fields->size()) return 0;
    return _world.readScriptField(id, (*fields)[field]).value_or(0);
  });
  // Writes to an entity spawned this frame apply once it exists.
  s.bind("__jmFieldSet", [this, fields](EntityId id, int32_t field, uint32_t bits) {
    if (field < 0 || static_cast<size_t>(field) >= fields->size()) return;
    const World::ScriptFieldRef ref = (*fields)[field];
    if (!_world.writeScriptField(id, ref, bits)) {
      _spawner.whenSpawned(id, [this, id, ref, bits]() { _world.writeScriptField(id, ref, bits); });
    }
  });

  // ---- Script params (this script's, or another entity's) ------------------------------
  s.bind("__jmParamNumber", [](ScriptCall& call, std::string key, double fallback) {
    return paramNumber(call.params(), key, fallback);
  });
  s.bind("__jmParamString", [](ScriptCall& call, std::string key) { return paramString(call.params(), key); });
  s.bind("__jmEntityParamNumber", [this](EntityId id, std::string key, double fallback) {
    const nlohmann::json* params = paramsOf(id);
    return params ? paramNumber(*params, key, fallback) : fallback;
  });
  s.bind("__jmEntityParamString", [this](EntityId id, std::string key) -> std::optional<std::string> {
    const nlohmann::json* params = paramsOf(id);
    return params ? paramString(*params, key) : std::nullopt;
  });

  // ---- Messages between scripts (delivered before the receiver's next update) ----------
  s.bind("__jmEntitySend", [this](ScriptCall& call, EntityId to, std::string name, std::string text, double number) {
    _scriptManager.queueMessage(to, ScriptMessage{call.self(), std::move(name), std::move(text), number});
  });
  s.bind("__jmMessageFrom", [](ScriptCall& call) {
    return call.script.message ? call.script.message->from : kNoEntity;
  });
  s.bind("__jmMessageName", [](ScriptCall& call) -> std::optional<std::string> {
    return call.script.message ? std::optional(call.script.message->name) : std::nullopt;
  });
  s.bind("__jmMessageText", [](ScriptCall& call) -> std::optional<std::string> {
    return call.script.message ? std::optional(call.script.message->text) : std::nullopt;
  });
  s.bind("__jmMessageNumber", [](ScriptCall& call) { return call.script.message ? call.script.message->number : 0.0; });

  // ---- Data files (any text asset, e.g. JSON listed in the manifest) --------------------
  s.bind("__jmDataRead", [this](std::string path) -> std::optional<std::string> {
    try {
      const std::vector<uint8_t> bytes = _assetManager.readFile(_manifest.resolve(path, ".json"));
      return std::string(bytes.begin(), bytes.end());
    } catch (const std::exception& e) {
      JM_LOG_ERROR("[script] data file '{}' can't be read: {}", path, e.what());
      return std::nullopt;
    }
  });

  // ---- Time --------------------------------------------------------------------------
  s.bind("__jmTimeScale", [this]() { return _clock.scale(); });
  s.bind("__jmTimeSetScale", [this](float scale) { _clock.setScale(scale); });
  s.bind("__jmTimeElapsed", [this]() { return _clock.elapsed(); });
  s.bind("__jmTimeUnscaledElapsed", [this]() { return _clock.unscaledElapsed(); });
  s.bind("__jmTimeUnscaledDelta", [this]() { return _clock.unscaledDt(); });

  // ---- Game state (store 0 = session, 1 = save file, 2+ = an entity's data) -------------
  // A released or unknown store reads as empty and ignores writes.
  auto store = [this](int32_t which) -> GameState* {
    if (which == 0) return &_session;
    if (which == 1) return _save.get();
    return _entityStores.find(which);
  };
  s.bind("__jmEntityStore", [this](EntityId id) { return alive(_world, id) ? _entityStores.idFor(id) : -1; });
  s.bind("__jmStateGetNumber", [store](int32_t which, std::string key, double fallback) {
    GameState* state = store(which);
    return state ? state->getNumber(key, fallback) : fallback;
  });
  s.bind("__jmStateSetNumber", [store](int32_t which, std::string key, double value) {
    if (GameState* state = store(which)) state->setNumber(key, value);
  });
  s.bind("__jmStateGetString", [store](int32_t which, std::string key) -> std::optional<std::string> {
    GameState* state = store(which);
    return state ? state->getString(key) : std::nullopt;
  });
  s.bind("__jmStateSetString", [store](int32_t which, std::string key, std::string value) {
    if (GameState* state = store(which)) state->setString(key, std::move(value));
  });
  s.bind("__jmStateGetJson", [store](int32_t which, std::string key) -> std::optional<std::string> {
    GameState* state = store(which);
    auto value = state ? state->getJson(key) : std::nullopt;
    return value ? std::optional<std::string>(value->dump()) : std::nullopt;
  });
  s.bind("__jmStateSetJson", [store](int32_t which, std::string key, std::string json) {
    nlohmann::json value = nlohmann::json::parse(json, nullptr, false);
    if (value.is_discarded()) {
      JM_LOG_ERROR("[script] state '{}': not valid JSON", key);
      return;
    }
    if (GameState* state = store(which)) state->setJson(key, std::move(value));
  });
  // A JSON array of the keys starting with `prefix`.
  s.bind("__jmStateKeys", [store](int32_t which, std::string prefix) -> std::optional<std::string> {
    GameState* state = store(which);
    return nlohmann::json(state ? state->keys(prefix) : std::vector<std::string>{}).dump();
  });
  s.bind("__jmStateHas", [store](int32_t which, std::string key) {
    GameState* state = store(which);
    return state && state->has(key);
  });
  s.bind("__jmStateRemove", [store](int32_t which, std::string key) {
    if (GameState* state = store(which)) state->remove(key);
  });
  s.bind("__jmStateClear", [store](int32_t which) {
    if (GameState* state = store(which)) state->clear();
  });

  // ---- Scenes (requests apply at the end of the frame) ---------------
  s.bind("__jmSceneLoad", [this](std::string scene) {
    _sceneManager.requestLoad(_manifest.resolve(scene, ".scene.json"));
  });
  s.bind("__jmSceneTransition", [this](std::string scene, float seconds, std::string shader) {
    _sceneManager.requestTransition(_manifest.resolve(scene, ".scene.json"),
                                    TransitionConfig{seconds, shader.empty() ? shader : _manifest.resolve(shader, ".frag")});
  });
  s.bind("__jmSceneIsTransitioning", [this]() { return _sceneManager.isTransitioning(); });
  s.bind("__jmSceneSpawnGroup", [this](std::string group) { _sceneManager.requestGroup(std::move(group), true); });
  s.bind("__jmSceneDespawnGroup", [this](std::string group) { _sceneManager.requestGroup(std::move(group), false); });
  s.bind("__jmSceneGroupSpawned", [this](std::string group) { return _sceneManager.groupSpawned(group); });
  s.bind("__jmSceneCurrent", [this]() -> std::optional<std::string> { return _sceneManager.getCurrentScenePath(); });

  // ---- App -------------------------------------------------------------------------
  s.bind("__jmAppQuit", [this]() { _eventBus.emit(EVT_AppQuit, events::Quit{}); });
}
