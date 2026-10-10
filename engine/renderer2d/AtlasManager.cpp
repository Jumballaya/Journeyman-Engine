#include "AtlasManager.hpp"

#include "../core/logger/logging.hpp"

namespace {

// Mirrors AssetManager's path canonicalization, so lookups by path match its dedup.
std::string canonicalKey(const std::filesystem::path& path) { return path.lexically_normal().generic_string(); }

}  // namespace

bool AtlasManager::loadAtlas(AssetHandle handle, const std::filesystem::path& sourcePath, TextureHandle texture,
                             uint32_t width, uint32_t height,
                             const std::unordered_map<std::string, std::array<int, 4>>& pixelRegions) {
  if (width == 0 || height == 0 || !texture.isValid()) {
    JM_LOG_ERROR("[AtlasManager] {} is {}x{} with {} texture; not registered", sourcePath.string(), width, height,
                 texture.isValid() ? "a" : "no");
    return false;
  }
  AtlasInfo info{texture, width, height, {}, std::nullopt};
  const glm::vec2 size(width, height);
  for (const auto& [name, rect] : pixelRegions) {
    // Out-of-range rects still register: the visible artifact helps find the packing bug.
    if (rect[0] < 0 || rect[1] < 0 || rect[2] <= 0 || rect[3] <= 0 || rect[0] + rect[2] > static_cast<int>(width) ||
        rect[1] + rect[3] > static_cast<int>(height)) {
      JM_LOG_WARN("[AtlasManager] {} region '{}' [{}, {}, {}, {}] is out of bounds for {}x{}", sourcePath.string(), name,
                  rect[0], rect[1], rect[2], rect[3], width, height);
    }
    info.regions.emplace(name, glm::vec4(glm::vec2(rect[0], rect[1]) / size, glm::vec2(rect[2], rect[3]) / size));
  }
  auto old = _atlases.find(handle);
  const bool moved = old != _atlases.end() && old->second.regions != info.regions;
  _atlases[handle] = std::move(info);
  _pathIndex[canonicalKey(sourcePath)] = handle;
  return moved;
}

std::optional<std::pair<TextureHandle, glm::vec4>> AtlasManager::lookup(AssetHandle atlasHandle,
                                                                        std::string_view region) const {
  auto it = _atlases.find(atlasHandle);
  if (it == _atlases.end()) return std::nullopt;
  auto found = it->second.regions.find(std::string(region));
  if (found == it->second.regions.end()) return std::nullopt;
  return std::make_pair(it->second.texture, found->second);
}

std::optional<std::pair<TextureHandle, glm::vec4>> AtlasManager::lookupByPath(std::string_view atlasPath,
                                                                              std::string_view region) const {
  auto it = _pathIndex.find(canonicalKey(atlasPath));
  return it == _pathIndex.end() ? std::nullopt : lookup(it->second, region);
}

bool AtlasManager::clearDynamicAtlas(AssetHandle atlasHandle) {
  auto it = _atlases.find(atlasHandle);
  if (it == _atlases.end() || !it->second.shelf) return false;
  it->second.regions.clear();
  it->second.shelf = jm::atlas::ShelfPackerState{it->second.width, it->second.height};
  return true;
}

TextureHandle AtlasManager::removeAtlas(AssetHandle atlasHandle) {
  auto it = _atlases.find(atlasHandle);
  if (it == _atlases.end()) return {};
  const TextureHandle texture = it->second.texture;
  std::erase_if(_pathIndex, [&](const auto& entry) { return entry.second == atlasHandle; });
  _atlases.erase(it);
  return texture;
}
