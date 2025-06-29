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

  runConverters(_assets.at(handle), handle);

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

void AssetManager::addAssetConverter(const std::vector<std::string>& extensions, ConverterCallback callback) {
  ConverterCallbackHandle cbHandle{_nextCallbackId++};
  _callbacks.emplace(cbHandle, std::move(callback));

  std::hash<std::string> hasher;
  for (const auto& ext : extensions) {
    FileExtensionHandle extHandle{static_cast<uint32_t>(hasher(ext))};
    _extensionToCallbackHandle.emplace(extHandle, cbHandle);
  }
}

void AssetManager::runConverters(const RawAsset& asset, const AssetHandle& handle) {
  std::string ext = asset.filePath.extension().string();

  std::hash<std::string> hasher;
  FileExtensionHandle extHandle{static_cast<uint32_t>(hasher(ext))};

  auto it = _extensionToCallbackHandle.find(extHandle);
  if (it == _extensionToCallbackHandle.end()) {
    return;
  }
  ConverterCallbackHandle cbHandle = it->second;
  auto cbIt = _callbacks.find(cbHandle);
  if (cbIt == _callbacks.end()) {
    return;
  }

  cbIt->second(asset, handle);
}