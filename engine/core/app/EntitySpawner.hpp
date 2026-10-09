#pragma once

#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"
#include "../ecs/prefab/Prefab.hpp"
#include "SceneLoader.hpp"

class SceneManager;

// Prefab spawning safe while systems iterate: spawn() reserves an id and queues;
// flush() (main thread, between frames) instantiates and applies deferred destroys.
class EntitySpawner {
 public:
  EntitySpawner(World& world, AssetManager& assets, SceneManager& scenes);

  // Returns the id the entity will have once flushed. `x, y` set the position
  // (the prefab's z is kept); `overrides` merge into its components, except
  // "tags": [...], which are added to the entity.
  // `by`: the entity whose script asked, if any.
  EntityId spawn(const std::string& prefabPath, float x, float y,
                 nlohmann::json overrides = nlohmann::json::object(), EntityId by = kNoEntityId);

  // Told about each entity flush() builds, after it exists (whenSpawned
  // changes applied): its prefab, position and overrides as requested.
  struct Spawned {
    EntityId id;
    const std::string& prefabPath;
    float x, y;
    const nlohmann::json& overrides;
    EntityId by;
  };
  void addListener(std::function<void(const Spawned&)> listener) { _listeners.push_back(std::move(listener)); }

  // Runs `change` once `id` (spawned this frame) is instantiated; false (and
  // nothing queued) if `id` isn't waiting to spawn.
  bool whenSpawned(EntityId id, std::function<void()> change);

  // Attaches `child` to `parent` (kNoEntityId detaches) at the next flush,
  // after this frame's spawns, keeping it where it is.
  void attach(EntityId child, EntityId parent);

  void flush();

  // The prefab at `path`, loaded once and cached; null (error reported) if it
  // can't be loaded.
  const Prefab* prefab(const std::string& path);

 private:
  struct Request {
    EntityId id;
    std::string prefabPath;
    float x, y;
    nlohmann::json overrides;
    std::vector<std::function<void()>> changes;  // from whenSpawned
    nlohmann::json requested;  // overrides as asked for (for listeners)
    EntityId by;
  };

  World& _world;
  AssetManager& _assets;
  SceneManager& _scenes;

  std::vector<Request> _requests;
  std::vector<std::pair<EntityId, EntityId>> _attachments;  // child, parent
  SceneLoader _children;  // builds prefabs' children
  std::unordered_map<std::string, Prefab> _prefabCache;
  std::unordered_set<std::string> _missingPrefabs;  // reported once, then skipped
  std::vector<std::function<void(const Spawned&)>> _listeners;
};
