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

  // A file's bytes straight from the mounted folder or archive: no caching, no
  // converters, so it is safe from any thread. Throws if the file is missing.
  std::vector<uint8_t> readFile(const std::filesystem::path& filePath) const {
    return _fileSystem.read(filePath);
  }

  // Whether a converter can run again on an asset it already converted,
  // replacing what it made under the same handle (hot reload).
  enum class Reload { No, InPlace };

  // Folder mode: converters by extension ({".png"}), case-insensitive, all run
  // in registration order; one that throws doesn't stop the others.
  void addAssetConverter(const std::vector<std::string>& extensions, ConverterCallback callback,
                         Reload reload = Reload::No);

  // Archive mode: converters by the entry's type ("image"); when present it
  // replaces extension dispatch, so modules register both.
  void addAssetTypeConverter(std::string assetType, ConverterCallback callback);

  // An archive entry's metadata; nullopt in folder mode or if it has none.
  std::optional<nlohmann::json> metadataOf(const std::filesystem::path& path) const;

  // A handle for a runtime-made resource (e.g. a dynamic atlas); it has no
  // bytes, so getRawAsset on it throws.
  AssetHandle reserveSyntheticHandle();

  // Hot reload (folder mode): re-reads each loaded asset whose file changed
  // since it was read, and whose converters all reload in place, and runs them
  // again on its handle. Returns the paths reloaded; an archive never changes.
  std::vector<std::string> reloadChanged();

 private:
  std::unordered_map<AssetHandle, RawAsset> _assets;
  std::unordered_map<std::string, AssetHandle> _pathToHandle;
  struct Converter {
    ConverterCallback convert;
    Reload reload;
  };
  std::unordered_map<std::string, std::vector<Converter>> _converters;
  std::unordered_map<AssetHandle, std::filesystem::file_time_type> _modified;  // folder mode
  std::unordered_map<std::string, std::vector<ConverterCallback>> _typeConverters;
  FileSystem _fileSystem;
  uint32_t _nextAssetId = 1;

  void runConverters(const RawAsset& asset, const AssetHandle& handle);
  // The extension converters for a file name: ".png", or ".ui.html" and ".html" for "hud.ui.html".
  std::vector<const std::vector<Converter>*> extensionConverters(const std::filesystem::path& path) const;
};
