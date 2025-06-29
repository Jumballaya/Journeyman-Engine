#include <iostream>

#include "core/app/Application.hpp"
#include "core/assets/AssetManager.hpp"
#include "core/assets/FileSystem.hpp"
#include "core/assets/RawAsset.hpp"
#include "core/ecs/component/Component.hpp"

struct Position : public Component<Position> {
  COMPONENT_NAME("PositionComponent");
  Position(float _x, float _y) : x(_x), y(_y) {}
  float x = 0.0f;
  float y = 0.0f;
};

int main(int argc, char** argv) {
  std::cout << "Journeyman Engine Starting up...\n";

  std::filesystem::path rootPath = ".jm.json";
  if (argc > 1) {
    rootPath = argv[1];
  }

  std::filesystem::path rootDir;
  std::filesystem::path manifestPath;

  // Running bundled archive
  if (rootPath.extension() == ".jm") {
    std::cout << "[Archive] Archive mode not yet implemented\n";
    return 1;
  }

  // Running game config file
  if (rootPath.extension() == ".json") {
    rootDir = rootPath.parent_path();
    manifestPath = rootPath;
    std::cout << "[JSON] Mounting: " << rootDir << std::endl;
    std::cout << "[JSON] Manifest: " << manifestPath << std::endl;
  }
  // Running bundled folder
  else if (std::filesystem::is_directory(rootPath)) {
    rootDir = rootPath;
    manifestPath = ".jm.json";
    std::cout << "[Directory] Mounting: " << rootDir << std::endl;
    std::cout << "[Directory] Manifest: " << manifestPath << std::endl;
  } else {
    std::cerr << "Unknown input type. Must be a .json, directory, or .jm archive.\n";
    return 1;
  }

  Application app(rootDir, manifestPath);
  app.getWorld().registerComponent<Position>(
      [](World& world, EntityId id, const nlohmann::json& json) {
        std::cout << "Position deserialized\n";
        float x = json["x"].get<float>();
        float y = json["y"].get<float>();
        world.addComponent<Position>(id, x, y);
      });
  app.initialize();
  app.run();

  std::cout << "Journeyman Engine Shutting down...\n";
  return 0;
}