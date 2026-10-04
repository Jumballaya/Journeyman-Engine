#include "AssetManager.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "../logger/logging.hpp"

namespace {

// Lowercased converter key: ".PNG" and ".png" match.
std::string normalizeExt(std::string ext) {
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

// Dedup key: normalized so "./foo" and "foo" match.
std::string canonicalPathKey(const std::filesystem::path& p) {
  return p.lexically_normal().generic_string();
}

}  // namespace

AssetManager::AssetManager(const std::filesystem::path& root) {
  // A .jm file mounts an archive; anything else mounts a folder (same rule as
  // Application's argv parsing).
  if (std::filesystem::is_regular_file(root) && root.extension() == ".jm") {
    _fileSystem.mountArchive(root);
  } else {
    _fileSystem.mountFolder(root);
  }
}

AssetHandle AssetManager::loadAsset(const std::filesystem::path& filePath) {
  // path::operator/ drops the mount root for absolute paths, which would work
  // from a folder but break in archives.
  if (filePath.is_absolute()) {
    JM_LOG_ERROR("AssetManager: absolute path not allowed: '{}'", filePath.string());
    throw std::runtime_error("AssetManager: absolute path not allowed: " + filePath.string());
  }

  const std::string key = canonicalPathKey(filePath);

  if (auto it = _pathToHandle.find(key); it != _pathToHandle.end()) {
    return it->second;
  }

  RawAsset asset = loadRawBytes(filePath);

  AssetHandle handle{_nextAssetId++};
  _assets.emplace(handle, std::move(asset));
  _pathToHandle.emplace(key, handle);

  runConverters(_assets.at(handle), handle);

  return handle;
}

const RawAsset& AssetManager::getRawAsset(const AssetHandle& handle) const {
  auto it = _assets.find(handle);
  if (it == _assets.end()) {
    JM_LOG_ERROR("AssetManager: Invalid AssetHandle");
    throw std::runtime_error("AssetManager: Invalid AssetHandle.");
  }
  return it->second;
}

RawAsset AssetManager::loadRawBytes(const std::filesystem::path& filePath) {
  RawAsset asset;
  asset.filePath = filePath;
  asset.data = _fileSystem.read(filePath);
  return asset;
}

void AssetManager::addAssetConverter(
    const std::vector<std::string>& extensions,
    ConverterCallback callback) {
  for (const auto& ext : extensions) {
    _converters[normalizeExt(ext)].push_back(callback);
  }
}

void AssetManager::addAssetTypeConverter(std::string assetType, ConverterCallback callback) {
  _typeConverters[std::move(assetType)].push_back(std::move(callback));
}

std::optional<nlohmann::json> AssetManager::metadataOf(const std::filesystem::path& path) const {
  return _fileSystem.metadataOf(path);
}

AssetHandle AssetManager::reserveSyntheticHandle() {
  AssetHandle handle{_nextAssetId++};
  return handle;
}

void AssetManager::runConverters(const RawAsset& asset, const AssetHandle& handle) {
  // A typed archive entry with a type converter uses only that; otherwise
  // (folder mode, untyped or unknown type) dispatch by extension.
  auto typeOpt = _fileSystem.typeOf(asset.filePath);
  if (typeOpt.has_value()) {
    auto it = _typeConverters.find(*typeOpt);
    if (it != _typeConverters.end()) {
      for (auto& cb : it->second) {
        try {
          cb(asset, handle);
        } catch (const std::exception& e) {
          JM_LOG_ERROR("[AssetManager] type converter '{}' threw for '{}': {}",
                       *typeOpt, asset.filePath.string(), e.what());
        } catch (...) {
          JM_LOG_ERROR("[AssetManager] type converter '{}' threw unknown exception for '{}'",
                       *typeOpt, asset.filePath.string());
        }
      }
      return;
    }
    JM_LOG_WARN("[AssetManager] archive entry '{}' has type '{}' but no converter registered; falling back to extension dispatch",
                asset.filePath.string(), *typeOpt);
  }

  // Every compound suffix, longest first: "hud.ui.html" fires ".ui.html" and
  // ".html" converters (path::extension() would only see ".html").
  const std::string filename = asset.filePath.filename().string();
  std::string lowered(filename.size(), '\0');
  std::transform(filename.begin(), filename.end(), lowered.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  for (size_t pos = lowered.find('.'); pos != std::string::npos;
       pos = lowered.find('.', pos + 1)) {
    const std::string suffix = lowered.substr(pos);
    auto it = _converters.find(suffix);
    if (it == _converters.end()) continue;

    // A throwing converter is logged; the others still run.
    for (auto& cb : it->second) {
      try {
        cb(asset, handle);
      } catch (const std::exception& e) {
        JM_LOG_ERROR("[AssetManager] converter threw for '{}': {}",
                     asset.filePath.string(), e.what());
      } catch (...) {
        JM_LOG_ERROR("[AssetManager] converter threw unknown exception for '{}'",
                     asset.filePath.string());
      }
    }
  }
}
