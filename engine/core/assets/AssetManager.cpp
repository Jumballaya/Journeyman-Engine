#include "AssetManager.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "../logger/logging.hpp"

namespace {

// Converter keys are lowercase: ".PNG" and ".png" match.
std::string lowercase(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return s;
}

// A throwing converter is logged; the others still run.
void runEach(const std::vector<ConverterCallback>& converters, const RawAsset& asset, const AssetHandle& handle) {
  for (const auto& convert : converters) {
    try {
      convert(asset, handle);
    } catch (const std::exception& e) {
      JM_REPORT_ERROR((ErrorSource{asset.filePath.generic_string()}), "[AssetManager] converter threw for '{}': {}", asset.filePath.string(), e.what());
    } catch (...) {
      JM_LOG_ERROR("[AssetManager] converter threw unknown exception for '{}'", asset.filePath.string());
    }
  }
}

}  // namespace

AssetManager::AssetManager(const std::filesystem::path& root) {
  // A file mounts an archive (a .jm, or a game executable with one appended);
  // a directory mounts a folder.
  if (std::filesystem::is_regular_file(root)) {
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

  const std::string key = FileSystem::key(filePath);
  if (auto it = _pathToHandle.find(key); it != _pathToHandle.end()) return it->second;

  RawAsset asset{_fileSystem.read(filePath), filePath};
  const AssetHandle handle{_nextAssetId++};
  _pathToHandle.emplace(key, handle);
  if (auto modified = _fileSystem.modified(filePath)) _modified[handle] = *modified;
  runConverters(_assets.emplace(handle, std::move(asset)).first->second, handle);
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

void AssetManager::addAssetConverter(const std::vector<std::string>& extensions, ConverterCallback callback,
                                     Reload reload) {
  for (const auto& ext : extensions) _converters[lowercase(ext)].push_back({callback, reload});
}

void AssetManager::addAssetTypeConverter(std::string assetType, ConverterCallback callback) {
  _typeConverters[std::move(assetType)].push_back(std::move(callback));
}

std::optional<nlohmann::json> AssetManager::metadataOf(const std::filesystem::path& path) const {
  return _fileSystem.metadataOf(path);
}

AssetHandle AssetManager::reserveSyntheticHandle() { return AssetHandle{_nextAssetId++}; }

void AssetManager::runConverters(const RawAsset& asset, const AssetHandle& handle) {
  // A typed archive entry with a type converter uses only that; otherwise
  // (folder mode, untyped or unknown type) dispatch by extension.
  if (auto type = _fileSystem.typeOf(asset.filePath)) {
    if (auto it = _typeConverters.find(*type); it != _typeConverters.end()) return runEach(it->second, asset, handle);
    JM_LOG_WARN("[AssetManager] archive entry '{}' has type '{}' but no converter registered; falling back to extension dispatch",
                asset.filePath.string(), *type);
  }

  for (const auto* converters : extensionConverters(asset.filePath)) {
    for (const auto& c : *converters) runEach({c.convert}, asset, handle);
  }
}

std::vector<const std::vector<AssetManager::Converter>*> AssetManager::extensionConverters(
    const std::filesystem::path& path) const {
  // Every compound suffix, longest first (path::extension() would only see ".html").
  std::vector<const std::vector<Converter>*> found;
  const std::string filename = lowercase(path.filename().string());
  for (size_t pos = filename.find('.'); pos != std::string::npos; pos = filename.find('.', pos + 1)) {
    if (auto it = _converters.find(filename.substr(pos)); it != _converters.end()) found.push_back(&it->second);
  }
  return found;
}

AssetManager::Reloaded AssetManager::reloadChanged() {
  // Collected first: a converter may load other assets, which changes the maps.
  std::vector<std::pair<AssetHandle, bool>> changed;  // and whether it needs a scene restart
  for (auto& [handle, modified] : _modified) {
    const RawAsset& asset = _assets.at(handle);
    const auto now = _fileSystem.modified(asset.filePath);
    if (!now || *now == modified) continue;  // gone for a moment (a build swapping folders): look again later
    bool reloads = true, restart = false, converted = false;
    for (const auto* converters : extensionConverters(asset.filePath)) {
      for (const auto& c : *converters) {
        converted = true;
        reloads &= c.reload != Reload::No;
        restart |= c.reload == Reload::RestartScene;
      }
    }
    // No converters (a scene, a prefab, data): read when a scene starts.
    if (reloads) changed.emplace_back(handle, restart || !converted);
  }
  Reloaded reloaded;
  for (const auto [handle, restart] : changed) {
    RawAsset& asset = _assets.at(handle);
    // Timed before and after: a file replaced while it was read is read again next time.
    const auto before = _fileSystem.modified(asset.filePath);
    auto bytes = _fileSystem.tryRead(asset.filePath);
    if (!before || !bytes || _fileSystem.modified(asset.filePath) != before) continue;
    _modified[handle] = *before;
    if (*bytes == asset.data) continue;  // rewritten, not changed (a rebuild)
    asset.data = std::move(*bytes);
    runConverters(asset, handle);
    reloaded.paths.push_back(asset.filePath.generic_string());
    reloaded.restartScene |= restart;
  }
  return reloaded;
}
