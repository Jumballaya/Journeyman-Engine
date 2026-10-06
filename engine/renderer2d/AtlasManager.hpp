#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

#include <glm/glm.hpp>

#include "../core/assets/AssetHandle.hpp"
#include "../core/assets/AssetManager.hpp"
#include "../core/logger/logging.hpp"
#include "ShelfPacker.hpp"
#include "TextureHandle.hpp"

// Texture atlases: named regions as normalized UV rects, looked up by atlas
// handle or path ("atlas.json#region"). Static (packed by jm) or dynamic (glyphs).
class AtlasManager {
 public:
  // Registers a packed atlas whose image is already uploaded as `texture`;
  // re-registering a handle replaces it.
  void loadAtlas(AssetHandle handle, const std::filesystem::path& sourcePath, TextureHandle texture, uint32_t width,
                 uint32_t height, const std::unordered_map<std::string, std::array<int, 4>>& pixelRegions);

  // An empty atlas filled by addRegion; filter "linear" or else "nearest".
  // Main thread only; Renderer is a template so tests can fake the GPU.
  template <typename Renderer>
  AssetHandle createDynamicAtlas(AssetManager& assets, Renderer& renderer, uint32_t width, uint32_t height,
                                 std::string_view filter);

  // Packs and uploads RGBA8 pixels into a dynamic atlas; its UV rect, or nullopt
  // if the atlas is unknown/static/full, the name is taken or the size is bad.
  template <typename Renderer>
  std::optional<glm::vec4> addRegion(Renderer& renderer, AssetHandle atlasHandle, std::string_view name,
                                     const void* pixels, uint32_t width, uint32_t height);

  // nullopt if the atlas or region is unknown.
  std::optional<std::pair<TextureHandle, glm::vec4>> lookup(AssetHandle atlasHandle, std::string_view region) const;
  // Same, by the atlas's manifest path.
  std::optional<std::pair<TextureHandle, glm::vec4>> lookupByPath(std::string_view atlasPath,
                                                                  std::string_view region) const;

  size_t atlasCount() const noexcept { return _atlases.size(); }
  bool hasAtlas(AssetHandle handle) const noexcept { return _atlases.find(handle) != _atlases.end(); }

 private:
  struct AtlasInfo {
    TextureHandle texture;
    uint32_t width = 0;
    uint32_t height = 0;
    std::unordered_map<std::string, glm::vec4> regions;  // [u, v, w, h] in normalized UV space
    std::optional<jm::atlas::ShelfPackerState> shelf;     // dynamic atlases only
  };

  std::unordered_map<AssetHandle, AtlasInfo> _atlases;
  std::unordered_map<std::string, AssetHandle> _pathIndex;  // canonical path → handle
};

template <typename Renderer>
AssetHandle AtlasManager::createDynamicAtlas(AssetManager& assets, Renderer& renderer, uint32_t width, uint32_t height,
                                             std::string_view filter) {
  const TextureHandle texture = width > 0 && height > 0 ? renderer.createEmptyTexture(static_cast<int>(width),
                                                                                    static_cast<int>(height), filter)
                                                        : TextureHandle{};
  if (!texture.isValid()) {
    JM_LOG_ERROR("[AtlasManager] couldn't create a {}x{} dynamic atlas", width, height);
    return AssetHandle{};
  }
  const AssetHandle handle = assets.reserveSyntheticHandle();
  _atlases[handle] = AtlasInfo{texture, width, height, {}, jm::atlas::ShelfPackerState{width, height}};
  return handle;
}

template <typename Renderer>
std::optional<glm::vec4> AtlasManager::addRegion(Renderer& renderer, AssetHandle atlasHandle, std::string_view name,
                                                 const void* pixels, uint32_t width, uint32_t height) {
  auto it = _atlases.find(atlasHandle);
  if (it == _atlases.end() || !it->second.shelf) {
    JM_LOG_ERROR("[AtlasManager] addRegion: not a dynamic atlas (see createDynamicAtlas)");
    return std::nullopt;
  }
  AtlasInfo& info = it->second;
  const std::string key(name);
  if (!pixels || info.regions.contains(key)) {
    JM_LOG_ERROR("[AtlasManager] addRegion: '{}' has no pixels or is already in the atlas", key);
    return std::nullopt;
  }
  const auto slot = jm::atlas::packShelf(*info.shelf, width, height);
  if (!slot) {
    JM_LOG_WARN("[AtlasManager] addRegion: atlas full (cannot fit {}x{})", width, height);
    return std::nullopt;
  }
  if (!renderer.subUploadTexture(info.texture, static_cast<int>(slot->x), static_cast<int>(slot->y),
                                 static_cast<int>(width), static_cast<int>(height), pixels)) {
    return std::nullopt;
  }
  const glm::vec2 size(info.width, info.height);
  const glm::vec4 uv(glm::vec2(slot->x, slot->y) / size, glm::vec2(width, height) / size);
  info.regions.emplace(key, uv);
  return uv;
}
