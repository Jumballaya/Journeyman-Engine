#include <algorithm>
#include <fstream>
#include <unordered_map>

#include "../../logger/logging.hpp"
#include "../Engine.hpp"
#include "../Platform.hpp"
#include "AgentRun.hpp"

// JM_INSPECT: the entry scene as the engine builds it, for tools that show a
// scene (jm's scene view) without knowing how the engine reads one:
//   {"scene", "entities": [{"id", "name", "parent", "origin", "components", "world"}],
//    "skipped": [{"entry", "reason"}], "draw", "camera", "error"}
// origin is where the entity's entry is (Moment.cpp's: scene, entry key,
// prefab); components the JSON each was made from (prefab and overrides
// merged); world its transform's live fields (where it is, its parents'
// places applied); draw and camera a frame of it as the modules draw it, no
// step run (the state dump's: each item its entity's). An entry that's a
// prefab is a scene of one of it (SceneManager::loadPrefabScene). A scene
// that fails to load has no entities, and says why in error.
void AgentRun::inspect() {
  const std::string scene = _engine.entrySceneName();
  nlohmann::json out = {{"scene", scene}, {"entities", nlohmann::json::array()}, {"skipped", nlohmann::json::array()}};
  try {
    SceneManager& scenes = _engine.getSceneManager();
    if (scene.ends_with(".prefab.json")) scenes.loadPrefabScene(scene);
    else scenes.loadScene(scene);
    _engine.frame(0.0f);  // drawn, nothing stepped (not simulating)
  } catch (const std::exception& e) {
    out["error"] = e.what();
  }
  nlohmann::json state = _engine.stateJson();
  std::unordered_map<std::string, nlohmann::json> live;  // by id, as the state writes it
  for (nlohmann::json& entity : state["entities"]) live[entity["id"].dump()] = std::move(entity["components"]);
  World& world = _engine.getWorld();
  std::vector<EntityId> ids = world.entities();
  std::sort(ids.begin(), ids.end(), [](EntityId a, EntityId b) { return a.index < b.index; });  // as built
  for (const EntityId id : ids) {
    nlohmann::json origin = originOf(id);
    if (!origin.is_object() || origin.value("from", "") != "scene") continue;  // not the scene's: a module's own (a server's scripts)
    nlohmann::json entity = {{"id", idJson(id)}, {"origin", std::move(origin)}};
    if (const auto& o = entity["origin"]; o.contains("name")) entity["name"] = o["name"];
    entity["parent"] = idJson(world.parentOf(id));
    const nlohmann::json* components = world.componentJson(id);
    entity["components"] = components ? *components : nlohmann::json::object();
    if (const nlohmann::json& now = live[entity["id"].dump()]; now.contains("TransformComponent"))
      entity["world"] = now["TransformComponent"];
    out["entities"].push_back(std::move(entity));
  }
  for (const char* part : {"draw", "camera"})
    if (state.contains(part)) out[part] = std::move(state[part]);
  for (const auto& skipped : _engine.getSceneManager().loader().skipped())
    out["skipped"].push_back({{"entry", skipped.key}, {"reason", skipped.reason}});
  std::string error;
  if (!platform::writeAtomically(_dev.inspectOut, out.dump() + "\n", error)) JM_LOG_ERROR("[Engine] JM_INSPECT: {}", error);
}
