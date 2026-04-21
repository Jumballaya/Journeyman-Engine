#include "SceneLoader.hpp"

#include "../ecs/prefab/Prefab.hpp"
#include "../ecs/prefab/PrefabLoader.hpp"

SceneLoader::SceneLoader(World &world, AssetManager &assetManager)
    : _world(world), _assetManager(assetManager) {}

void SceneLoader::loadScene(const std::filesystem::path &scenePath) {
  AssetHandle handle = _assetManager.loadAsset(scenePath);
  const RawAsset &asset = _assetManager.getRawAsset(handle);
  _currentSceneName = scenePath.filename().string();
  parseScene(asset);
}

void SceneLoader::loadScene(const AssetHandle &handle) {
  const RawAsset &asset = _assetManager.getRawAsset(handle);
  _currentSceneName = asset.filePath.string();
  parseScene(asset);
}

const std::string &SceneLoader::getCurrentSceneName() const {
  return _currentSceneName;
}

void SceneLoader::parseScene(const RawAsset &asset) {
  std::string jsonString(asset.data.begin(), asset.data.end());
  nlohmann::json sceneJson = nlohmann::json::parse(jsonString);
  if (sceneJson.contains("name")) {
    _currentSceneName = sceneJson["name"].get<std::string>();
  }
  if (sceneJson.contains("entities")) {
    for (const auto &entityJson : sceneJson["entities"]) {
      createEntityFromJson(entityJson);
    }
  }
}

void SceneLoader::createEntityFromJson(const nlohmann::json &entityJson) {
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
    return;
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
      if (info && info->jsonDeserialize) {
        info->jsonDeserialize(_world, entity, componentData);
      }
    }
  }
}
