// F8's moment: the person holds the run to show their agent "this thing,
// here". The game doesn't step, hear its devices or play sound while held, so
// a recorded play has no held frames: it replays the same as if it never was.
// What's picked is saved with a marker: an entity with where it came from
// (its origin) and its state; a point or an area with what's there.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>

#include "../../logger/logging.hpp"
#include "../../scripting/ScriptComponent.hpp"
#include "../Engine.hpp"
#include "AgentRun.hpp"

namespace {

double hundredths(float v) { return std::round(static_cast<double>(v) * 100.0) / 100.0; }

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
  _note.clear();
}

void AgentRun::resume() {
  if (!_held) return;
  _held = _stepHeld = _cleanPending = false;
  _picks.clear();
  _note.clear();
  std::error_code ec;
  if (!_dev.recordDir.empty()) std::filesystem::remove(heldImage(), ec);
}

void AgentRun::pick(const Pick& pick, bool add) {
  const auto at = std::ranges::find(_picks, pick);
  if (!add) _picks.clear();
  if (add && at != _picks.end()) _picks.erase(at);
  else if (pick.kind != Pick::Kind::Entity || pick.entity != kNoEntityId) _picks.push_back(pick);
}

std::vector<AgentRun::Pick> AgentRun::picks() const {
  std::vector<Pick> alive;
  for (const Pick& pick : _picks)  // a `.` may have ended an entity
    if (pick.kind != Pick::Kind::Entity || _engine.getWorld().isAlive(pick.entity)) alive.push_back(pick);
  return alive;
}

bool AgentRun::setNote(size_t n, std::string note) {
  if (n == 0) {
    _note = std::move(note);
    return true;
  }
  for (Pick& pick : _picks) {  // the nth alive, as picks() numbers them
    if (pick.kind == Pick::Kind::Entity && !_engine.getWorld().isAlive(pick.entity)) continue;
    if (--n == 0) {
      pick.note = std::move(note);
      return true;
    }
  }
  return false;
}

std::string AgentRun::noteOf(size_t n) const {
  if (n == 0) return _note;
  const auto alive = picks();
  return n <= alive.size() ? alive[n - 1].note : std::string();
}

nlohmann::json AgentRun::pickJson(const Pick& pick) {
  nlohmann::json out;
  switch (pick.kind) {
    case Pick::Kind::Entity:
      out = {{"kind", "entity"}, {"entity", idJson(pick.entity)}};
      break;
    case Pick::Kind::Point:
      out = {{"kind", "point"}, {"at", {hundredths(pick.lo.x), hundredths(pick.lo.y)}}};
      break;
    case Pick::Kind::Area:
      out = {{"kind", "area"}, {"area", {hundredths(pick.lo.x), hundredths(pick.lo.y), hundredths(pick.hi.x), hundredths(pick.hi.y)}}};
      break;
  }
  if (!pick.note.empty()) out["note"] = pick.note;
  return out;
}

int AgentRun::saveMoment(const std::string& note) {
  const int n = mark(note.empty() ? _note : note, true);
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
  for (const Pick& pick : withPicks ? picks() : std::vector<Pick>{}) {
    nlohmann::json saved = pickJson(pick);
    if (pick.kind == Pick::Kind::Entity) {
      const auto entity = std::ranges::find_if(state["entities"], [&](const nlohmann::json& e) { return e["id"] == saved["entity"]; });
      if (entity == state["entities"].end()) continue;
      saved.update({{"origin", entity->value("origin", nlohmann::json())}, {"tags", (*entity)["tags"]}, {"state", (*entity)["components"]}});
    } else {
      saved["scene"] = state.value("scene", std::string());
    }
    if (_describe) saved.update(_describe(pick));
    picked.push_back(std::move(saved));
  }
  // The frame the marker is about: the one running (F8, seen as it ends), or
  // between frames (the driver's marker) the last one run, as the state says.
  const uint64_t frame = state.value("frame", uint64_t{0});
  const nlohmann::json game = withPicks && _gameArea ? _gameArea() : nlohmann::json();
  const auto marker = _recorder->marker(frame, _engine.getClock().unscaledElapsed(), state, note, picked, game);
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
    for (const Pick& pick : picks()) {
      picked.push_back(pickJson(pick));
      if (pick.kind == Pick::Kind::Entity)
        picked.back().update({{"origin", originOf(pick.entity)}, {"tags", _engine.getWorld().tagNames(pick.entity)}});
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
