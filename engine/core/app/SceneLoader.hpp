#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "../assets/AssetHandle.hpp"
#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"

class SceneLoader {
 public:
  SceneLoader(World& world, AssetManager& assetManager);

  std::vector<EntityId> loadScene(const std::filesystem::path& scenePath);
  std::vector<EntityId> loadScene(const AssetHandle& sceneHandle);

  const std::string& getCurrentSceneName() const;

 private:
  std::string _currentSceneName;

  World& _world;
  AssetManager& _assetManager;

  std::vector<EntityId> parseScene(const RawAsset& asset);
  EntityId createEntityFromJson(const nlohmann::json& entityJson);
};