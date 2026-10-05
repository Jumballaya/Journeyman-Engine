#include "SceneLoader.hpp"

#include "../ecs/prefab/Prefab.hpp"
#include "../ecs/prefab/PrefabLoader.hpp"

SceneLoader::SceneLoader(World &world, AssetManager &assetManager)
    : _world(world), _assetManager(assetManager) {}

std::vector<EntityId>
SceneLoader::loadScene(const std::filesystem::path &scenePath) {
  AssetHandle handle = _assetManager.loadAsset(scenePath);
  const RawAsset &asset = _assetManager.getRawAsset(handle);
  _currentSceneName = scenePath.filename().string();
  return parseScene(asset);
}

std::vector<EntityId> SceneLoader::loadScene(const AssetHandle &handle) {
  const RawAsset &asset = _assetManager.getRawAsset(handle);
  _currentSceneName = asset.filePath.string();
  return parseScene(asset);
}

const std::string &SceneLoader::getCurrentSceneName() const {
  return _currentSceneName;
}

std::vector<EntityId> SceneLoader::parseScene(const RawAsset &asset) {
  std::vector<EntityId> created;
  std::string jsonString(asset.data.begin(), asset.data.end());
  nlohmann::json sceneJson = nlohmann::json::parse(jsonString);
  if (sceneJson.contains("name")) {
    _currentSceneName = sceneJson["name"].get<std::string>();
  }
  _groups.clear();
  if (sceneJson.contains("entities")) {
    // Roll back this scene's entities if one fails, so none are left unowned.
    try {
      for (const auto &entityJson : sceneJson["entities"]) {
        if (auto group = entityJson.value("group", std::string()); !group.empty()) {
          _groups[group].push_back(entityJson);
          continue;
        }
        if (!conditionsHold(entityJson)) continue;
        created.push_back(createEntityFromJson(entityJson));
      }
    } catch (...) {
      for (EntityId id : created) {
        _world.destroyEntity(id);
      }
      throw;
    }
  }
  return created;
}

bool SceneLoader::conditionsHold(const nlohmann::json &entityJson) const {
  if (!_condition) return true;
  if (auto key = entityJson.value("if", std::string()); !key.empty() && !_condition(key)) return false;
  if (auto key = entityJson.value("unless", std::string()); !key.empty() && _condition(key)) return false;
  return true;
}

EntityId SceneLoader::createEntityFromJson(const nlohmann::json &entityJson) {
  if (entityJson.contains("prefab")) {
    std::filesystem::path prefabPath = entityJson["prefab"].get<std::string>();
    AssetHandle handle = _assetManager.loadAsset(prefabPath);
    const RawAsset &asset = _assetManager.getRawAsset(handle);
    Prefab prefab = PrefabLoader::loadFromBytes(asset.data);

    EntityId entity =
        entityJson.contains("overrides")
            ? _world.instantiatePrefab(prefab, entityJson["overrides"])
            : _world.instantiatePrefab(prefab);

    if (entityJson.contains("name")) {
      _world.addTag(entity, entityJson["name"].get<std::string>());
    }
    return entity;
  }

  EntityId entity = _world.createEntity();

  if (entityJson.contains("name")) {
    std::string name = entityJson["name"].get<std::string>();
    _world.addTag(entity, name);
  }

  if (entityJson.contains("components")) {
    auto components = entityJson["components"];
    for (const auto &[componentName, componentData] : components.items()) {
      auto maybeId =
          _world.getComponentRegistry().getComponentIdByName(componentName);
      if (!maybeId.has_value())
        continue;

      ComponentId id = maybeId.value();
      const ComponentInfo *info = _world.getComponentRegistry().getInfo(id);
      if (info && info->addFromJson) {
        info->addFromJson(_world, entity, componentData);
      }
    }
  }

  return entity;
}
