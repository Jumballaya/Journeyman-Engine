#include "AtlasManager.hpp"

#include "../core/logger/logging.hpp"

namespace {

// Mirrors AssetManager's path canonicalization so path-keyed lookups behave
// identically to AssetManager's path dedup.
std::string canonicalKey(std::string_view path) {
  return std::filesystem::path(path).lexically_normal().generic_string();
}

}  // namespace

void AtlasManager::loadAtlas(AssetHandle handle,
                             const std::filesystem::path& sourcePath,
                             TextureHandle texture,
                             uint32_t width, uint32_t height,
                             const std::unordered_map<std::string, std::array<int, 4>>& pixelRegions) {
  if (width == 0 || height == 0) {
    JM_LOG_ERROR("[AtlasManager] {} has zero dimensions ({}x{}); refusing to register",
                 sourcePath.string(), width, height);
    return;
  }
  if (!texture.isValid()) {
    JM_LOG_ERROR("[AtlasManager] {} has invalid texture handle; refusing to register",
                 sourcePath.string());
    return;
  }

  AtlasInfo info;
  info.texture = texture;
  info.width = width;
  info.height = height;
  info.regions.reserve(pixelRegions.size());

  const float fw = static_cast<float>(width);
  const float fh = static_cast<float>(height);

  for (const auto& [name, rect] : pixelRegions) {
    // Out-of-range pixel rects log but still register, so the visible artifact
    // (sampling outside the atlas) helps diagnose a baker bug without crashing
    // the scene.
    if (rect[0] < 0 || rect[1] < 0 || rect[2] <= 0 || rect[3] <= 0 ||
        rect[0] + rect[2] > static_cast<int>(width) ||
        rect[1] + rect[3] > static_cast<int>(height)) {
      JM_LOG_WARN("[AtlasManager] {} region '{}' rect [{}, {}, {}, {}] is out of bounds for {}x{}; "
                  "registering anyway, sampling will wrap",
                  sourcePath.string(), name, rect[0], rect[1], rect[2], rect[3], width, height);
    }
    info.regions.emplace(name, glm::vec4{
        static_cast<float>(rect[0]) / fw,
        static_cast<float>(rect[1]) / fh,
        static_cast<float>(rect[2]) / fw,
        static_cast<float>(rect[3]) / fh,
    });
  }

  const std::string key = sourcePath.lexically_normal().generic_string();
  _atlases[handle] = std::move(info);
  _pathIndex[key] = handle;
}

std::optional<std::pair<TextureHandle, glm::vec4>>
AtlasManager::lookup(AssetHandle atlasHandle, std::string_view region) const {
  auto it = _atlases.find(atlasHandle);
  if (it == _atlases.end()) {
    return std::nullopt;
  }
  // string_view → string allocates once per lookup. Acceptable for
  // scene-deserialize (one-shot) use; switch to a heterogeneous-lookup map
  // (C++20 transparent comparator) if a hot-path caller appears.
  auto rit = it->second.regions.find(std::string(region));
  if (rit == it->second.regions.end()) {
    return std::nullopt;
  }
  return std::make_pair(it->second.texture, rit->second);
}

std::optional<std::pair<TextureHandle, glm::vec4>>
AtlasManager::lookupByPath(std::string_view atlasPath, std::string_view region) const {
  auto pit = _pathIndex.find(canonicalKey(atlasPath));
  if (pit == _pathIndex.end()) {
    return std::nullopt;
  }
  return lookup(pit->second, region);
}
