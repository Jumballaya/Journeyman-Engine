#include "EntitySpawner.hpp"

#include "../ecs/prefab/PrefabLoader.hpp"
#include "../logger/logging.hpp"
#include "SceneManager.hpp"

EntitySpawner::EntitySpawner(World& world, AssetManager& assets, SceneManager& scenes)
    : _world(world), _assets(assets), _scenes(scenes) {}

EntityId EntitySpawner::spawn(const std::string& prefabPath, float x, float y, nlohmann::json overrides) {
  std::lock_guard lock(_mutex);
  EntityId id = _world.createEntity();
  _requests.push_back(Request{id, prefabPath, x, y, std::move(overrides), {}});
  return id;
}

bool EntitySpawner::whenSpawned(EntityId id, std::function<void()> change) {
  std::lock_guard lock(_mutex);
  for (auto& req : _requests) {
    if (req.id != id) continue;
    req.changes.push_back(std::move(change));
    return true;
  }
  return false;
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
    try {
      // Position override: keep the prefab's authored z (draw order).
      float z = 0.0f;
      for (const auto& [name, data] : p->components) {
        if (name == "TransformComponent" && data.contains("position") &&
            data["position"].is_array() && data["position"].size() >= 3 && data["position"][2].is_number()) {
          z = data["position"][2].get<float>();
        }
      }
      if (!req.overrides.is_object()) req.overrides = nlohmann::json::object();
      const nlohmann::json tags = req.overrides.value("tags", nlohmann::json::array());
      req.overrides.erase("tags");
      auto& transform = req.overrides["TransformComponent"];
      if (!transform.is_object()) transform = nlohmann::json::object();
      transform["position"] = {req.x, req.y, z};

      _world.instantiatePrefabInto(req.id, *p, req.overrides);
      for (const auto& tag : tags) {
        if (tag.is_string()) _world.addTag(req.id, tag.get<std::string>());
      }
      _scenes.adoptEntity(req.id);
      for (auto& change : req.changes) change();
    } catch (const std::exception& e) {
      JM_LOG_ERROR("[EntitySpawner] instantiate '{}' failed: {}", req.prefabPath, e.what());
      _scenes.destroyEntity(req.id);  // a throwing `change` runs after adoption
    }
  }

  for (EntityId id : _world.takePendingDestroys()) _scenes.destroyEntity(id);
}
