// F8's moment: the person holds the run to show their agent "this thing,
// here". The game doesn't step, hear its devices or play sound while held, so
// a recorded play has no held frames: it replays the same as if it never was.
// What's picked is saved with a marker, each pick with where its entity came
// from (its origin) and its state.

#include <algorithm>
#include <filesystem>
#include <functional>

#include "../../logger/logging.hpp"
#include "../../scripting/ScriptComponent.hpp"
#include "../Engine.hpp"
#include "AgentRun.hpp"

namespace {

nlohmann::json idJson(EntityId id) { return {id.index, id.generation}; }

nlohmann::json failure(std::string message) { return {{"ok", false}, {"error", std::move(message)}}; }

}  // namespace

bool AgentRun::moment() {
  // The editor holds its own game (its Pause); an unrecorded run has nowhere to keep a moment.
  if (_engine.embedded() || !_recorder) {
    dropMarker();
    return true;
  }
  hold();
  return true;
}

void AgentRun::hold() {
  if (_held) return;
  _held = true;
  _cleanPending = true;  // this frame's picture, before its marks are drawn
  _picks.clear();
}

void AgentRun::resume() {
  if (!_held) return;
  _held = _stepHeld = _cleanPending = false;
  _picks.clear();
  std::error_code ec;
  if (!_dev.recordDir.empty()) std::filesystem::remove(heldImage(), ec);
}

void AgentRun::pick(EntityId id, bool add) {
  const auto at = std::ranges::find(_picks, id);
  if (!add) _picks.clear();
  if (add && at != _picks.end()) _picks.erase(at);
  else if (id != kNoEntityId) _picks.push_back(id);
}

std::vector<EntityId> AgentRun::picks() const {
  std::vector<EntityId> alive;
  for (const EntityId id : _picks)
    if (_engine.getWorld().isAlive(id)) alive.push_back(id);  // a `.` may have ended one
  return alive;
}

int AgentRun::saveMoment(const std::string& note) {
  const int n = mark(note, true);
  resume();
  return n;
}

int AgentRun::mark(const std::string& note, bool withPicks) {
  if (!_recorder) {
    if (!_notRecordingNotice.empty()) _engine.notify(_notRecordingNotice);
    return 0;
  }
  const nlohmann::json state = _engine.stateJson();
  nlohmann::json picked = nlohmann::json::array();
  for (const EntityId id : withPicks ? picks() : std::vector<EntityId>{}) {
    for (const nlohmann::json& entity : state["entities"]) {
      if (entity["id"] != idJson(id)) continue;
      picked.push_back({{"entity", entity["id"]},
                        {"origin", entity.value("origin", nlohmann::json())},
                        {"tags", entity["tags"]},
                        {"state", entity["components"]}});
      if (nlohmann::json rect = _whereDrawn ? _whereDrawn(id) : nullptr; !rect.is_null()) picked.back()["rect"] = rect;
    }
  }
  // The frame the marker is about: the one running (F8, seen as it ends), or
  // between frames (the driver's marker) the last one run, as the state says.
  const uint64_t frame = state.value("frame", uint64_t{0});
  const auto marker = _recorder->marker(frame, _engine.getClock().unscaledElapsed(), state, note, picked);
  std::error_code ec;
  if (_held && std::filesystem::exists(heldImage(), ec)) {  // the frame as it was, without the moment's marks
    std::filesystem::copy_file(heldImage(), marker.image, std::filesystem::copy_options::overwrite_existing, ec);
  } else if (_engine.stepping()) {
    _engine.requestCapture({marker.image});
  } else {  // between steps (the driver's): the frame on screen, now
    nlohmann::json reply;
    _engine.getModules().driveCommand(_engine, "capture", marker.image.string(), reply);
  }
  _engine.notify("marker " + std::to_string(marker.n) + " saved");
  JM_LOG_INFO("[Session] marker {} at frame {}{}", marker.n, frame,
              picked.empty() ? "" : " with " + std::to_string(picked.size()) + " picked");
  return marker.n;
}

nlohmann::json AgentRun::momentCommand(const std::string& verb, const std::string& args) {
  // A driven run draws only when stepped: the moment's frame is drawn again to show it.
  auto redrawn = [this](nlohmann::json reply) {
    if (_dev.drive) _engine.frame(0.0f);
    return reply;
  };
  if (verb == "freeze") {
    if (_engine.frameCount() == 0) return failure("no frame has run yet: step first, then freeze");
    hold();
    return redrawn({{"ok", true}, {"frame", _engine.frameCount() - 1}});  // the last run: what's on screen
  }
  if (verb == "resume") {
    resume();
    return redrawn({{"ok", true}});
  }
  if (!_held) return failure(verb + ": not frozen (freeze first)");
  if (verb == "picks") {
    nlohmann::json picked = nlohmann::json::array();
    for (const EntityId id : picks()) {
      picked.push_back({{"entity", idJson(id)}, {"origin", originOf(id)}, {"tags", _engine.getWorld().tagNames(id)}});
    }
    return {{"ok", true}, {"frame", _engine.frameCount() - 1}, {"picks", std::move(picked)}};
  }
  // save
  const size_t picked = picks().size();
  const int n = saveMoment(args);
  if (n == 0) return redrawn(failure("this run isn't recorded (JM_RECORD_DIR): nothing to save (resumed)"));
  return redrawn({{"ok", true}, {"marker", n}, {"picks", picked}});
}

nlohmann::json AgentRun::originOf(EntityId id) const {
  const auto it = _origins.find(id);
  return it == _origins.end() ? nlohmann::json() : it->second;
}

void AgentRun::trackOrigins() {
  SceneManager& scenes = _engine.getSceneManager();
  scenes.onEntityBuilt([this, &scenes](EntityId id, const nlohmann::json& entry, const std::string& scene) {
    nlohmann::json origin = {{"from", "scene"}, {"scene", scene}};
    if (const std::string& key = scenes.loader().entryKey(); !key.empty()) origin["entry"] = key;  // "3", "3/c1", "g:wave1/0"
    if (entry.contains("name")) origin["name"] = entry["name"];
    if (entry.contains("prefab")) origin["prefab"] = entry["prefab"];
    keepOrigin(id, std::move(origin));
  });
  _engine.getSpawner().addListener([this](const EntitySpawner::Spawned& s) {
    World& world = _engine.getWorld();
    nlohmann::json origin = {{"from", "spawn"}, {"prefab", s.prefabPath}, {"frame", _engine.frameCount()}};
    if (s.by != kNoEntityId) {
      origin["by"] = idJson(s.by);
      // The spawner's script as it was then: a hot reload may change it later.
      const ScriptComponent* script = world.getComponent<ScriptComponent>(s.by);
      const ScriptInstance* instance = script ? _engine.getScriptManager().getInstance(script->instance) : nullptr;
      if (instance) origin["script"] = instance->scriptPath();
    }
    // Its prefab's children came with it.
    std::function<void(EntityId)> parts = [&](EntityId parent) {
      for (const EntityId child : world.childrenOf(parent)) {
        nlohmann::json part = origin;
        part["in"] = idJson(s.id);
        keepOrigin(child, std::move(part));
        parts(child);
      }
    };
    parts(s.id);
    keepOrigin(s.id, std::move(origin));
  });
}

void AgentRun::keepOrigin(EntityId id, nlohmann::json origin) {
  _origins[id] = std::move(origin);
  if (_origins.size() < 2 * _originsPruned + 256) return;
  std::erase_if(_origins, [this](const auto& kept) { return !_engine.getWorld().isAlive(kept.first); });
  _originsPruned = _origins.size();
}
