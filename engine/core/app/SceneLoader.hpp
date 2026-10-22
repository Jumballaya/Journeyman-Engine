#pragma once

#include <filesystem>
#include <functional>
#include <optional>
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
  // Told about each entity built from an entry, as it's made (its children
  // not yet): the entry, and the path of the scene last loaded.
  using Built = std::function<void(EntityId id, const nlohmann::json& entry, const std::string& scene)>;
  void onBuilt(Built built) { _built = std::move(built); }

  // All or nothing: if an entry fails, the entities made so far are destroyed and it rethrows.
  std::vector<EntityId> loadScene(const std::filesystem::path& scenePath);
  std::vector<EntityId> loadScene(const AssetHandle& sceneHandle);
  // A scene's JSON, as the scene at path (what its entities' origins name).
  std::vector<EntityId> loadScene(const nlohmann::json& scene, const std::string& path);

  // One scene entry: {"name", "components": {...}} or {"name", "prefab", "overrides"},
  // with the entries in its "children" (and its prefab's) attached to it; an
  // instance's "overrides": {"children": {"Sword": {...}}} changes its prefab's
  // children by name. Atomic: throws without leaving an entity behind.
  // `key`, if given, names the entry within its scene (see entryKey()).
  EntityId createEntityFromJson(const nlohmann::json& entityJson, const std::string& key = {});
  // Builds `entries` (a "children" list) attached to `parent`, with `overrides`
  // ({name: component overrides}) applied to the named ones. Throws on failure,
  // after destroying the children made so far.
  void createChildren(EntityId parent, const nlohmann::json& entries, const nlohmann::json& overrides,
                      const std::string& keyPrefix = {});
  // Whether an entry's "if" / "unless" conditions hold now; which fails ("if key").
  bool conditionsHold(const nlohmann::json& entityJson) const { return !unmetCondition(entityJson); }
  std::optional<std::string> unmetCondition(const nlohmann::json& entityJson) const;
  // While a scene's entry is being built: a name for it that's the same in
  // every process loading the scene ("3", a group's "g:wave1/0", children
  // "3/c1", a prefab's children "3/p0"), which multiplayer uses to pair up
  // copies of an entity. Empty for entities not built from a scene's entries.
  const std::string& entryKey() const { return _keys.empty() ? kNoKey : _keys.back(); }
  // The last loaded scene's held-back entries, by group.
  const std::unordered_map<std::string, std::vector<nlohmann::json>>& groups() const { return _groups; }
  // The last loaded scene's entries it didn't build, in order: their key and
  // why ("group wave1", "if key", "unless key").
  struct Skipped {
    std::string key, reason;
  };
  const std::vector<Skipped>& skipped() const { return _skipped; }

 private:
  World& _world;
  AssetManager& _assetManager;
  Condition _condition;
  Built _built;
  std::string _scene;  // the scene last loaded (its groups come later)
  std::unordered_map<std::string, std::vector<nlohmann::json>> _groups;
  std::vector<Skipped> _skipped;
  int _nesting = 0;
  std::vector<std::string> _keys;
  static inline const std::string kNoKey;  // entries being built, outermost first: a prefab holding itself would never end
};
