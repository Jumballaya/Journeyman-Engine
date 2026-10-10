#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "AssetConverter.hpp"
#include "AssetHandle.hpp"
#include "FileSystem.hpp"
#include "RawAsset.hpp"

// Loads raw bytes by manifest-relative path, dedupes them into AssetHandles and
// hands them to the converters modules register, which own the decoded forms.
class AssetManager {
 public:
  AssetManager(const std::filesystem::path& root = ".");

  // Repeat loads of a path return the cached handle; converters run once.
  // Absolute paths are rejected.
  AssetHandle loadAsset(const std::filesystem::path& filePath);

  const RawAsset& getRawAsset(const AssetHandle& handle) const;
  // Whether the mounted folder or archive has the file (loaded or not).
  bool exists(const std::filesystem::path& filePath) const { return _fileSystem.exists(filePath); }

  // A file's bytes straight from the mounted folder or archive: no caching, no
  // converters, so it is safe from any thread. Throws if the file is missing.
  std::vector<uint8_t> readFile(const std::filesystem::path& filePath) const {
    return _fileSystem.read(filePath);
  }

  // Folder mode: converters by extension ({".png"}), case-insensitive, all run
  // in registration order; one that throws doesn't stop the others.
  void addAssetConverter(const std::vector<std::string>& extensions, ConverterCallback callback);

  // Archive mode: converters by the entry's type ("image"); when present it
  // replaces extension dispatch, so modules register both.
  void addAssetTypeConverter(std::string assetType, ConverterCallback callback);

  // An archive entry's metadata; nullopt in folder mode or if it has none.
  std::optional<nlohmann::json> metadataOf(const std::filesystem::path& path) const;

  // A handle for a runtime-made resource (e.g. a dynamic atlas); it has no
  // bytes, so getRawAsset on it throws.
  AssetHandle reserveSyntheticHandle();

 private:
  std::unordered_map<AssetHandle, RawAsset> _assets;
  std::unordered_map<std::string, AssetHandle> _pathToHandle;
  std::unordered_map<std::string, std::vector<ConverterCallback>> _converters;
  std::unordered_map<std::string, std::vector<ConverterCallback>> _typeConverters;
  FileSystem _fileSystem;
  uint32_t _nextAssetId = 1;

  void runConverters(const RawAsset& asset, const AssetHandle& handle);
};
