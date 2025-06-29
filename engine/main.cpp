#include <iostream>

#include "core/app/Application.hpp"
#include "core/assets/AssetManager.hpp"
#include "core/assets/FileSystem.hpp"
#include "core/assets/RawAsset.hpp"

int main(int argc, char** argv) {
  std::cout << "Journeyman Engine Starting up...\n";

  std::filesystem::path manifestPath = ".jm.json";
  if (argc > 1) {
    manifestPath = argv[1];
  }
  std::cout << "Loading Game Manifest: " << manifestPath << std::endl;
  AssetManager assetManager("demo_game");

  assetManager.addAssetConverter({".json"}, [](const RawAsset& asset, const AssetHandle& handle) {
    std::cout << "Asset Converter JSON Ran\n";
    std::cout << "Path: " << asset.filePath << std::endl;
    std::cout << "ID: " << handle.id << std::endl;
  });

  AssetHandle assetId = assetManager.loadAsset(manifestPath);
  RawAsset asset = assetManager.getRawAsset(assetId);

  Application app;
  app.initialize(manifestPath);
  app.run();

  std::cout << "Journeyman Engine Shutting down...\n";
  return 0;
}