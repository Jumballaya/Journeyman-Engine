// The engine-level script API: logging, entities & world queries, component
// fields, spawning, script params, time, game state, scenes, app control.
// Feature modules bind their own APIs (audio, input, renderer, UI, window).
// The AssemblyScript side lives in cli/internal/stdlib/runtime/.

#include <iostream>
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

#include "../logger/logging.hpp"
#include "ApplicationEvents.hpp"
#include "Engine.hpp"

using host::AsString;
using host::ScriptCall;
using host::WasmBytes;

namespace {

bool alive(World& world, EntityId id) {
  return world.isAlive(id) && !world.isPendingDestroy(id);
}

}  // namespace

void Engine::bindScriptApi() {
  ScriptManager& s = _scriptManager;

  // ---- Logging -----------------------------------------------------------------
  s.bind("__jmLog", [](std::string message) {
    std::cout << "[script] " << message << "\n";
    JM_LOG_INFO("[script] {}", message);
  });
  // AssemblyScript calls this on a failed assertion / runtime error, then traps.
  s.bind("abort", [](AsString message, AsString file, int32_t line, int32_t column) {
    JM_LOG_ERROR("[script] abort: {} at {}:{}:{}", message.text, file.text, line, column);
    std::cerr << "[script] abort: " << message.text << " at " << file.text << ":" << line << ":" << column << "\n";
  });
  // Seeds AssemblyScript's Math.random().
  s.bind("seed", []() -> double {
    static std::random_device device;
    return static_cast<double>(device()) * 4294967296.0 + device();
  });

  // ---- Entities & world ----------------------------------------------------------
  s.bind("__jmSelf", [](ScriptCall& call) { return call.self(); });
  s.bind("__jmEntityIsAlive", [this](EntityId id) { return alive(_world, id); });
  s.bind("__jmEntityHasTag", [this](EntityId id, std::string tag) { return _world.hasTag(id, tag); });
  s.bind("__jmEntitySetTag", [this](EntityId id, std::string tag, bool present) {
    if (present) {
      _world.addTag(id, tag);
    } else {
      _world.removeTag(id, tag);
    }
  });
  s.bind("__jmWorldDestroy", [this](EntityId id) { _world.destroyDeferred(id); });
  s.bind("__jmWorldFindFirst", [this](std::string tag) {
    for (EntityId id : _world.findWithTag(tag)) {
      if (alive(_world, id)) return id;
    }
    return EntityId{UINT32_MAX, UINT32_MAX};
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
  // Raw 4-byte values; reads of a missing component give 0, writes are dropped.
  s.bind("__jmFieldGet", [this, fields](EntityId id, int32_t field) -> uint32_t {
    if (field < 0 || static_cast<size_t>(field) >= fields->size()) return 0;
    return _world.readScriptField(id, (*fields)[field]).value_or(0);
  });
  s.bind("__jmFieldSet", [this, fields](EntityId id, int32_t field, uint32_t bits) {
    if (field >= 0 && static_cast<size_t>(field) < fields->size()) _world.writeScriptField(id, (*fields)[field], bits);
  });

  // ---- Script params ---------------------------------------------------------------
  s.bind("__jmParamNumber", [](ScriptCall& call, std::string key, double fallback) {
    auto it = call.params().find(key);
    if (it == call.params().end()) return fallback;
    if (it->is_number()) return it->get<double>();
    if (it->is_boolean()) return it->get<bool>() ? 1.0 : 0.0;
    return fallback;
  });
  s.bind("__jmParamString", [](ScriptCall& call, std::string key) -> std::optional<std::string> {
    auto it = call.params().find(key);
    if (it == call.params().end() || !it->is_string()) return std::nullopt;
    return it->get<std::string>();
  });

  // ---- Time --------------------------------------------------------------------------
  s.bind("__jmTimeScale", [this]() { return _clock.scale(); });
  s.bind("__jmTimeSetScale", [this](float scale) { _clock.setScale(scale); });
  s.bind("__jmTimeElapsed", [this]() { return _clock.elapsed(); });
  s.bind("__jmTimeUnscaledElapsed", [this]() { return _clock.unscaledElapsed(); });
  s.bind("__jmTimeUnscaledDelta", [this]() { return _clock.unscaledDt(); });

  // ---- Game state (store 0 = session, 1 = save file) ---------------------------------
  auto store = [this](int32_t which) -> GameState& { return which == 1 ? *_save : _session; };
  s.bind("__jmStateGetNumber", [store](int32_t which, std::string key, double fallback) {
    return store(which).getNumber(key, fallback);
  });
  s.bind("__jmStateSetNumber", [store](int32_t which, std::string key, double value) {
    store(which).setNumber(key, value);
  });
  s.bind("__jmStateGetString", [store](int32_t which, std::string key) { return store(which).getString(key); });
  s.bind("__jmStateSetString", [store](int32_t which, std::string key, std::string value) {
    store(which).setString(key, std::move(value));
  });
  s.bind("__jmStateHas", [store](int32_t which, std::string key) { return store(which).has(key); });
  s.bind("__jmStateRemove", [store](int32_t which, std::string key) { store(which).remove(key); });
  s.bind("__jmStateClear", [store](int32_t which) { store(which).clear(); });

  // ---- Scenes (requests apply on the main thread at the end of the frame) ---------------
  s.bind("__jmSceneLoad", [this](std::string scene) {
    _sceneManager.requestLoad(_manifest.resolve(scene, ".scene.json"));
  });
  s.bind("__jmSceneTransition", [this](std::string scene, float seconds, std::string shader) {
    _sceneManager.requestTransition(_manifest.resolve(scene, ".scene.json"),
                                    TransitionConfig{seconds, shader.empty() ? shader : _manifest.resolve(shader, ".frag")});
  });
  s.bind("__jmSceneIsTransitioning", [this]() { return _sceneManager.isTransitioning(); });
  s.bind("__jmSceneCurrent", [this]() -> std::optional<std::string> { return _sceneManager.getCurrentScenePath(); });

  // ---- App -------------------------------------------------------------------------
  s.bind("__jmAppQuit", [this]() { _eventBus.emit(EVT_AppQuit, events::Quit{}); });
}
