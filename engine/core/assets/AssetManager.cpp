#include "AssetManager.hpp"

#include <stdexcept>

#include "AssetLoader.hpp"

AssetManager::AssetManager(const std::filesystem::path& root) {
  _fileSystem.mountFolder(root);
  AssetLoader::setFileSystem(&_fileSystem);
}

AssetHandle AssetManager::loadAsset(const std::filesystem::path& filePath) {
  RawAsset asset = loadRawAsset(filePath);

  AssetHandle handle{_nextAssetId++};
  _assets.emplace(handle, std::move(asset));
  return handle;
}

const RawAsset& AssetManager::getRawAsset(const AssetHandle& handle) const {
  auto it = _assets.find(handle);
  if (it == _assets.end()) {
    throw std::runtime_error("AssetManager: Invalid AssetHandle.");
  }
  return it->second;
}

RawAsset AssetManager::loadRawAsset(const std::filesystem::path& filePath) {
  return AssetLoader::loadRawBytes(filePath);
}