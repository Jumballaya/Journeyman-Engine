#pragma once

#include <filesystem>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "../assets/AssetHandle.hpp"
#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"

// Builds a scene's entities from its JSON. An entry may also say when it
// appears: "group" holds it back until its group is spawned (see
// SceneManager::spawnGroup), and "if" / "unless" name game-state keys that
// must be set / unset for it to spawn at all (a collected key never returns).
class SceneLoader {
 public:
  // Whether a game-state key is set (true, nonzero, non-empty).
  using Condition = std::function<bool(const std::string& key)>;

  SceneLoader(World& world, AssetManager& assetManager);
  void setCondition(Condition condition) { _condition = std::move(condition); }

  // All or nothing: if an entry fails, the entities made so far are destroyed and it rethrows.
  std::vector<EntityId> loadScene(const std::filesystem::path& scenePath);
  std::vector<EntityId> loadScene(const AssetHandle& sceneHandle);

  // One scene entry: {"name", "components": {...}} or {"name", "prefab", "overrides"},
  // with the entries in its "children" (and its prefab's) attached to it; an
  // instance's "overrides": {"children": {"Sword": {...}}} changes its prefab's
  // children by name. Atomic: throws without leaving an entity behind.
  EntityId createEntityFromJson(const nlohmann::json& entityJson);
  // Builds `entries` (a "children" list) attached to `parent`, with `overrides`
  // ({name: component overrides}) applied to the named ones. Throws on failure,
  // after destroying the children made so far.
  void createChildren(EntityId parent, const nlohmann::json& entries, const nlohmann::json& overrides);
  // Whether an entry's "if" / "unless" conditions hold now.
  bool conditionsHold(const nlohmann::json& entityJson) const;
  // The last loaded scene's held-back entries, by group.
  const std::unordered_map<std::string, std::vector<nlohmann::json>>& groups() const { return _groups; }

 private:
  World& _world;
  AssetManager& _assetManager;
  Condition _condition;
  std::unordered_map<std::string, std::vector<nlohmann::json>> _groups;
};
