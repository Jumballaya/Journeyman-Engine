#include "EntitySpawner.hpp"

#include "../ecs/prefab/PrefabLoader.hpp"
#include "../logger/logging.hpp"
#include "SceneManager.hpp"

EntitySpawner::EntitySpawner(World& world, AssetManager& assets, SceneManager& scenes)
    : _world(world), _assets(assets), _scenes(scenes) {}

EntityId EntitySpawner::spawn(const std::string& prefabPath, float x, float y,
                              nlohmann::json overrides) {
  std::lock_guard lock(_mutex);
  EntityId id = _world.createEntity();
  _requests.push_back(Request{id, prefabPath, x, y, std::move(overrides)});
  return id;
}

const Prefab* EntitySpawner::prefab(const std::string& path) {
  if (auto it = _prefabCache.find(path); it != _prefabCache.end()) return &it->second;
  try {
    AssetHandle handle = _assets.loadAsset(path);
    Prefab loaded = PrefabLoader::loadFromBytes(_assets.getRawAsset(handle).data);
    return &_prefabCache.emplace(path, std::move(loaded)).first->second;
  } catch (const std::exception& e) {
    JM_LOG_ERROR("[EntitySpawner] cannot load prefab '{}': {}", path, e.what());
    return nullptr;
  }
}

void EntitySpawner::flush() {
  std::vector<Request> requests;
  {
    std::lock_guard lock(_mutex);
    requests.swap(_requests);
  }

  for (auto& req : requests) {
    const Prefab* p = prefab(req.prefabPath);
    if (!p) {
      _world.destroyEntity(req.id);
      continue;
    }
    // Position override: keep the prefab's authored z (draw order).
    float z = 0.0f;
    for (const auto& [name, data] : p->components) {
      if (name == "TransformComponent" && data.contains("position") &&
          data["position"].is_array() && data["position"].size() >= 3) {
        z = data["position"][2].get<float>();
      }
    }
    if (!req.overrides.is_object()) req.overrides = nlohmann::json::object();
    req.overrides["TransformComponent"]["position"] = {req.x, req.y, z};

    try {
      _world.instantiatePrefabInto(req.id, *p, req.overrides);
      _scenes.adoptEntity(req.id);
    } catch (const std::exception& e) {
      JM_LOG_ERROR("[EntitySpawner] instantiate '{}' failed: {}", req.prefabPath, e.what());
    }
  }

  for (EntityId id : _world.takePendingDestroys()) {
    _scenes.destroyEntity(id);
  }
}
