// State dumps for tools (JM_DUMP_DIR, JM_DUMP_FRAMES): the game as data rather
// than pixels, so an agent can assert on what a headless run did.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <filesystem>
#include <fstream>

#include "../logger/logging.hpp"
#include "../scripting/ScriptComponent.hpp"
#include "Engine.hpp"

namespace {

nlohmann::json idJson(EntityId id) {
  return id == kNoEntityId ? nlohmann::json(nullptr) : nlohmann::json::array({id.index, id.generation});
}

// A float as its shortest faithful decimal (0.32, not 0.3199999928474426).
double tidy(float value) {
  char text[32];
  std::snprintf(text, sizeof(text), "%.7g", value);
  return std::strtod(text, nullptr);
}

}  // namespace

nlohmann::json Engine::stateJson(bool withModules) {
  nlohmann::json entities = nlohmann::json::array();
  for (EntityId id : _world.entities()) {
    nlohmann::json components = nlohmann::json::object();
    for (const std::string& name : _world.componentNames(id)) {
      // A component's script fields, the values scripts see (f32 or u32).
      nlohmann::json fields = nlohmann::json::object();
      if (const ComponentInfo* info = _world.getComponentRegistry().getInfoByName(name)) {
        for (uint32_t i = 0; i < info->scriptFields.size(); ++i) {
          const auto bits = _world.readScriptField(id, World::ScriptFieldRef{info, i});
          if (!bits) continue;
          if (info->scriptFields[i].integer) {
            fields[info->scriptFields[i].name] = *bits;
          } else {
            float value;
            std::memcpy(&value, &*bits, sizeof(value));
            fields[info->scriptFields[i].name] = tidy(value);
          }
        }
        // Fields that mean nothing while another is zero (no shadow) are left out.
        std::vector<std::string> unused;
        for (const ScriptField& f : info->scriptFields) {
          if (!f.dumpedWith.empty() && fields.value(f.dumpedWith, nlohmann::json(0)) == 0) unused.push_back(f.name);
        }
        for (const std::string& f : unused) fields.erase(f);
      }
      components[name] = std::move(fields);
    }
    if (const auto* script = _world.getComponent<ScriptComponent>(id)) {
      nlohmann::json& s = components["ScriptComponent"];
      if (const ScriptInstance* instance = _scriptManager.getInstance(script->instance)) {
        s["script"] = instance->scriptPath();
        s["failed"] = instance->failed();
      }
      if (!script->params.empty()) s["params"] = script->params;
    }
    nlohmann::json entity = {{"id", idJson(id)}, {"tags", _world.tagNames(id)}, {"components", std::move(components)}};
    if (const EntityId parent = _world.parentOf(id); parent != kNoEntityId) entity["parent"] = idJson(parent);
    if (_world.isPendingDestroy(id)) entity["destroying"] = true;
    entities.push_back(std::move(entity));
  }

  // The frame this state is from: the current one in a frame's dump, the
  // last one run between frames (the driver's state, the exit dump).
  const uint64_t frame = _inFrame || _frames == 0 ? _frames : _frames - 1;
  nlohmann::json state = {{"frame", frame},
                          {"time", _clock.elapsed()},
                          {"scene", _sceneManager.getCurrentScenePath()},
                          {"entities", std::move(entities)},
                          {"session", _session.values()},
                          {"save", _save ? _save->values() : nlohmann::json::object()}};
  if (!withModules) return state;
  if (_playback) {
    state["replay"] = {{"frames", _playback->frames()},
                       {"diverged", _divergedAt ? nlohmann::json(*_divergedAt) : nlohmann::json()}};
  }
  _modules.describeState(*this, state);
  return state;
}

void Engine::dumpState(const std::string& name) {
  if (_options.dev.dumpDir.empty()) return;
  std::error_code ec;
  std::filesystem::create_directories(_options.dev.dumpDir, ec);
  const auto path = _options.dev.dumpDir / name;
  std::ofstream out(path);
  out << stateJson().dump() << "\n";  // one line, as the driver answers: greppable
  if (out) JM_LOG_INFO("[Engine] state dumped to {}", path.string());
  else JM_LOG_ERROR("[Engine] couldn't write {}", path.string());
}
