#include "AssetLoader.hpp"

RawAsset AssetLoader::loadRawBytes(const std::filesystem::path& filePath) {
  if (!_fileSystem) {
    throw std::runtime_error("AssetLoader: FileSystem not set.");
  }

  RawAsset asset;
  asset.filePath = filePath;
  asset.data = _fileSystem->read(filePath);

  return asset;
}

void AssetLoader::setFileSystem(FileSystem* fs) {
  _fileSystem = fs;
}