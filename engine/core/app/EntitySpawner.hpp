#pragma once

#include <functional>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
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
  EntityId spawn(const std::string& prefabPath, float x, float y,
                 nlohmann::json overrides = nlohmann::json::object());

  // Runs `change` once `id` (spawned this frame) is instantiated; false (and
  // nothing queued) if `id` isn't waiting to spawn. Any thread.
  bool whenSpawned(EntityId id, std::function<void()> change);

  // Attaches `child` to `parent` (kNoEntityId detaches) at the next flush,
  // after this frame's spawns, keeping it where it is. Any thread.
  void attach(EntityId child, EntityId parent);

  void flush();

 private:
  struct Request {
    EntityId id;
    std::string prefabPath;
    float x, y;
    nlohmann::json overrides;
    std::vector<std::function<void()>> changes;  // from whenSpawned
  };

  const Prefab* prefab(const std::string& path);

  World& _world;
  AssetManager& _assets;
  SceneManager& _scenes;

  std::mutex _mutex;
  std::vector<Request> _requests;
  std::vector<std::pair<EntityId, EntityId>> _attachments;  // child, parent
  SceneLoader _children;  // builds prefabs' children
  std::unordered_map<std::string, Prefab> _prefabCache;
};
