#include <algorithm>
#include <fstream>

#include "../../logger/logging.hpp"
#include "../Engine.hpp"
#include "../Platform.hpp"
#include "AgentRun.hpp"

// JM_INSPECT: the entry scene as the engine builds it, for tools that show a
// scene (jm's scene view) without knowing how the engine reads one:
//   {"scene", "entities": [{"id", "name", "parent", "origin", "components"}],
//    "skipped": [{"entry", "reason"}], "error"}
// origin is where the entity's entry is (Moment.cpp's: scene, entry key,
// prefab); components the JSON each was made from (prefab and overrides
// merged). A scene that fails to load has no entities, and says why in error.
void AgentRun::inspect() {
  const std::string scene = _engine.entrySceneName();
  nlohmann::json out = {{"scene", scene}, {"entities", nlohmann::json::array()}, {"skipped", nlohmann::json::array()}};
  try {
    _engine.getSceneManager().loadScene(scene);
  } catch (const std::exception& e) {
    out["error"] = e.what();
  }
  World& world = _engine.getWorld();
  std::vector<EntityId> ids = world.entities();
  std::sort(ids.begin(), ids.end(), [](EntityId a, EntityId b) { return a.index < b.index; });  // as built
  for (const EntityId id : ids) {
    nlohmann::json entity = {{"id", idJson(id)}, {"origin", originOf(id)}};
    if (const auto& origin = entity["origin"]; origin.contains("name")) entity["name"] = origin["name"];
    entity["parent"] = idJson(world.parentOf(id));
    const nlohmann::json* components = world.componentJson(id);
    entity["components"] = components ? *components : nlohmann::json::object();
    out["entities"].push_back(std::move(entity));
  }
  for (const auto& skipped : _engine.getSceneManager().loader().skipped())
    out["skipped"].push_back({{"entry", skipped.key}, {"reason", skipped.reason}});
  std::string error;
  if (!platform::writeAtomically(_dev.inspectOut, out.dump() + "\n", error)) JM_LOG_ERROR("[Engine] JM_INSPECT: {}", error);
}
