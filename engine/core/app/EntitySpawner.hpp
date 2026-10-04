#pragma once

#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"
#include "../ecs/prefab/Prefab.hpp"

class SceneManager;

// Runtime prefab spawning for scripts. spawn() may be called while systems
// iterate (it only reserves an EntityId and queues the request); flush() runs
// on the main thread between frames, instantiates the prefabs, hands them to
// the current scene, and applies deferred destroys from World.
class EntitySpawner {
 public:
  EntitySpawner(World& world, AssetManager& assets, SceneManager& scenes);

  // Returns the id the entity will have once flushed. `x, y` set the position
  // (the prefab's z is kept); `overrides` merge into its components.
  EntityId spawn(const std::string& prefabPath, float x, float y,
                 nlohmann::json overrides = nlohmann::json::object());

  void flush();

 private:
  struct Request {
    EntityId id;
    std::string prefabPath;
    float x, y;
    nlohmann::json overrides;
  };

  const Prefab* prefab(const std::string& path);

  World& _world;
  AssetManager& _assets;
  SceneManager& _scenes;

  std::mutex _mutex;
  std::vector<Request> _requests;
  std::unordered_map<std::string, Prefab> _prefabCache;
};
