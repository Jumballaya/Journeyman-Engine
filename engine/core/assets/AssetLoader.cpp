#include "AssetLoader.hpp"

#include <iostream>

RawAsset AssetLoader::loadRawBytes(const std::filesystem::path& filePath) {
  if (!_fileSystem) {
    throw std::runtime_error("AssetLoader: FileSystem not set.");
  }

  RawAsset asset;
  asset.filePath = filePath;
  asset.data = _fileSystem->read(filePath);

  std::cout << "[Debug] Asset Name: " << asset.filePath.filename().string() << "\n";
  std::cout << "[Debug] RawAsset size: " << asset.data.size() << " bytes\n";
  std::cout << "[Debug] Original file size: " << std::filesystem::file_size(asset.filePath) << " bytes\n";

  return asset;
}

void AssetLoader::setFileSystem(FileSystem* fs) {
  _fileSystem = fs;
}