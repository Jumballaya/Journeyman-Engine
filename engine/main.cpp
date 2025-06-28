#include <iostream>

#include "core/app/Application.hpp"
#include "core/assets/AssetLoader.hpp"
#include "core/assets/FileSystem.hpp"
#include "core/assets/RawAsset.hpp"

int main(int argc, char** argv) {
  std::cout << "Journeyman Engine Starting up...\n";

  std::filesystem::path manifestPath = ".jm.json";
  if (argc > 1) {
    manifestPath = argv[1];
  }
  std::cout << "Loading Game Manifest: " << manifestPath << std::endl;
  FileSystem fs;
  fs.mountFolder("demo_game");
  AssetLoader::setFileSystem(&fs);
  RawAsset asset = AssetLoader::loadRawBytes(manifestPath);

  Application app;
  app.initialize(manifestPath);
  app.run();

  std::cout << "Journeyman Engine Shutting down...\n";
  return 0;
}