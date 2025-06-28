#pragma once

#include <filesystem>
#include <unordered_map>

#include "AssetHandle.hpp"
#include "FileSystem.hpp"
#include "RawAsset.hpp"

class AssetManager {
 public:
  AssetManager(const std::filesystem::path& root = ".");

  AssetHandle loadAsset(const std::filesystem::path& filePath);
  const RawAsset& getRawAsset(const AssetHandle& handle) const;

 private:
  std::unordered_map<AssetHandle, RawAsset> _assets;
  FileSystem _fileSystem;

  uint32_t _nextAssetId = 1;
  RawAsset loadRawAsset(const std::filesystem::path& filePath);
};