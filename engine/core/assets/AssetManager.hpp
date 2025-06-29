#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

#include "AssetConverter.hpp"
#include "AssetHandle.hpp"
#include "FileSystem.hpp"
#include "RawAsset.hpp"

class AssetManager {
 public:
  AssetManager(const std::filesystem::path& root = ".");

  AssetHandle loadAsset(const std::filesystem::path& filePath);
  const RawAsset& getRawAsset(const AssetHandle& handle) const;

  void addAssetConverter(const std::vector<std::string>& extensions, ConverterCallback callback);
  void runConverters(const RawAsset& asset, const AssetHandle& handle);

 private:
  std::unordered_map<AssetHandle, RawAsset> _assets;
  std::unordered_map<FileExtensionHandle, ConverterCallbackHandle> _extensionToCallbackHandle;
  std::unordered_map<ConverterCallbackHandle, ConverterCallback> _callbacks;
  FileSystem _fileSystem;

  uint32_t _nextAssetId = 1;
  uint32_t _nextCallbackId = 1;
  RawAsset loadRawAsset(const std::filesystem::path& filePath);
};