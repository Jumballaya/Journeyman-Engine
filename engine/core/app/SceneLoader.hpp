#pragma once

#include <filesystem>
#include <functional>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <string>
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

  std::vector<EntityId> loadScene(const std::filesystem::path& scenePath);
  std::vector<EntityId> loadScene(const AssetHandle& sceneHandle);

  const std::string& getCurrentSceneName() const;

  // One scene entry: {"name", "components": {...}} or {"name", "prefab", "overrides"}.
  EntityId createEntityFromJson(const nlohmann::json& entityJson);
  // Whether an entry's "if" / "unless" conditions hold now.
  bool conditionsHold(const nlohmann::json& entityJson) const;
  // The last loaded scene's held-back entries, by group.
  const std::unordered_map<std::string, std::vector<nlohmann::json>>& groups() const { return _groups; }

 private:
  std::string _currentSceneName;
  Condition _condition;
  std::unordered_map<std::string, std::vector<nlohmann::json>> _groups;

  World& _world;
  AssetManager& _assetManager;

  std::vector<EntityId> parseScene(const RawAsset& asset);
};