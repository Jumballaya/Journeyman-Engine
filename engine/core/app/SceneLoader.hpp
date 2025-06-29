#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>

#include "../assets/AssetHandle.hpp"
#include "../assets/AssetManager.hpp"
#include "../ecs/World.hpp"

class SceneLoader {
 public:
  SceneLoader(World& world, AssetManager& assetManager);

  void loadScene(const std::filesystem::path& scenePath);
  void loadScene(const AssetHandle& sceneHandle);

  const std::string& getCurrentSceneName() const;

 private:
  std::string _currentSceneName;  // @TODO: Move to Application and make this a static loader class

  World& _world;
  AssetManager& _assetManager;

  void parseScene(const RawAsset& asset);
  void createEntityFromJson(const nlohmann::json& entityJson);
};