#include "AssetManager.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "../logger/logging.hpp"

namespace {

// Canonicalize an extension string so ".PNG", ".png", and ".Png" all map to
// the same converter key. Keeps the leading dot if present.
std::string normalizeExt(std::string ext) {
  std::transform(ext.begin(), ext.end(), ext.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return ext;
}

// Canonical form of the user-supplied path for dedup. std::filesystem::path
// normalization handles "./foo" vs "foo" and trailing separators. We keep it
// as a string key so the map is cheap to query.
std::string canonicalPathKey(const std::filesystem::path& p) {
  return p.lexically_normal().generic_string();
}

}  // namespace

AssetManager::AssetManager(const std::filesystem::path& root) {
  // Dispatch by path shape: a regular file with the .jm extension means we're
  // mounting a packed archive; anything else (directory, missing path) means
  // folder-mode mount. The .jm detection mirrors Application::run's argv
  // parser so the two stay in sync.
  if (std::filesystem::is_regular_file(root) && root.extension() == ".jm") {
    _fileSystem.mountArchive(root);
  } else {
    _fileSystem.mountFolder(root);
  }
}

AssetHandle AssetManager::loadAsset(const std::filesystem::path& filePath) {
  // Paths are manifest-root-relative by contract. Reject absolute paths at
  // the boundary: std::filesystem::path::operator/ silently drops the mount
  // prefix when the right-hand side is absolute, so "works on my machine"
  // bugs would ship into archives with missing entries. See AssetManager.hpp
  // for the full path-convention contract.
  if (filePath.is_absolute()) {
    JM_LOG_ERROR("AssetManager: absolute path not allowed: '{}'", filePath.string());
    throw std::runtime_error("AssetManager: absolute path not allowed: " + filePath.string());
  }

  const std::string key = canonicalPathKey(filePath);

  // Dedup: if we've already loaded this path, hand back the same handle
  // without re-reading or re-running converters.
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

void AssetManager::runConverters(const RawAsset& asset, const AssetHandle& handle) {
  // Match against every compound suffix of the filename, longest first, so
  // "player.script.json" triggers a converter registered for ".script.json"
  // AND a converter registered for ".json" (if any). std::filesystem::path's
  // extension() only returns the last-dot suffix, which would miss compound
  // registrations like ".script.json".
  const std::string filename = asset.filePath.filename().string();
  std::string lowered(filename.size(), '\0');
  std::transform(filename.begin(), filename.end(), lowered.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

  for (size_t pos = lowered.find('.'); pos != std::string::npos;
       pos = lowered.find('.', pos + 1)) {
    const std::string suffix = lowered.substr(pos);
    auto it = _converters.find(suffix);
    if (it == _converters.end()) continue;

    // Each converter is isolated: a throwing converter is logged and the others
    // still run. The raw asset remains in storage and retrievable by handle.
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
