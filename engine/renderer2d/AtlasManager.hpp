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
  void loadAtlas(AssetHandle handle,
                 const std::filesystem::path& sourcePath,
                 TextureHandle texture,
                 uint32_t width, uint32_t height,
                 const std::unordered_map<std::string, std::array<int, 4>>& pixelRegions);

  // An empty atlas filled by addRegion; filter "linear" or else "nearest".
  // Main thread only; Renderer is a template so tests can fake the GPU.
  template <typename Renderer>
  AssetHandle createDynamicAtlas(AssetManager& assets, Renderer& renderer,
                                 uint32_t width, uint32_t height,
                                 std::string_view filter);

  // Packs and uploads RGBA8 pixels into a dynamic atlas; its UV rect, or nullopt
  // if the atlas is unknown/static/full, the name is taken or the size is bad.
  template <typename Renderer>
  std::optional<glm::vec4>
  addRegion(Renderer& renderer, AssetHandle atlasHandle,
            std::string_view name, const void* pixels,
            uint32_t width, uint32_t height);

  // nullopt if the atlas or region is unknown.
  std::optional<std::pair<TextureHandle, glm::vec4>>
  lookup(AssetHandle atlasHandle, std::string_view region) const;

  // Same, by the atlas's manifest path.
  std::optional<std::pair<TextureHandle, glm::vec4>>
  lookupByPath(std::string_view atlasPath, std::string_view region) const;

  size_t atlasCount() const noexcept { return _atlases.size(); }
  bool hasAtlas(AssetHandle handle) const noexcept { return _atlases.find(handle) != _atlases.end(); }

 private:
  struct AtlasInfo {
    TextureHandle texture;
    uint32_t width = 0;
    uint32_t height = 0;
    // [u, v, w, h] in normalized UV space.
    std::unordered_map<std::string, glm::vec4> regions;

    // Shelf-packing state; dynamic atlases only.
    bool dynamic = false;
    uint32_t shelfTop = 0;
    uint32_t shelfHeight = 0;
    uint32_t shelfCursor = 0;
  };

  std::unordered_map<AssetHandle, AtlasInfo> _atlases;
  // canonical path → handle
  std::unordered_map<std::string, AssetHandle> _pathIndex;
};

template <typename Renderer>
AssetHandle AtlasManager::createDynamicAtlas(AssetManager& assets, Renderer& renderer,
                                             uint32_t width, uint32_t height,
                                             std::string_view filter) {
  if (width == 0 || height == 0) {
    JM_LOG_ERROR("[AtlasManager] createDynamicAtlas: invalid dimensions {}x{}", width, height);
    return AssetHandle{};
  }

  TextureHandle texture = renderer.createEmptyTexture(static_cast<int>(width),
                                                     static_cast<int>(height),
                                                     filter);
  if (!texture.isValid()) {
    JM_LOG_ERROR("[AtlasManager] createDynamicAtlas: GL allocation failed");
    return AssetHandle{};
  }

  AssetHandle handle = assets.reserveSyntheticHandle();
  AtlasInfo info;
  info.texture = texture;
  info.width = width;
  info.height = height;
  info.dynamic = true;
  info.shelfTop = 0;
  info.shelfHeight = 0;
  info.shelfCursor = 0;
  _atlases.emplace(handle, std::move(info));
  return handle;
}

template <typename Renderer>
std::optional<glm::vec4>
AtlasManager::addRegion(Renderer& renderer, AssetHandle atlasHandle,
                        std::string_view name, const void* pixels,
                        uint32_t width, uint32_t height) {
  auto it = _atlases.find(atlasHandle);
  if (it == _atlases.end()) {
    JM_LOG_ERROR("[AtlasManager] addRegion: unknown atlas handle");
    return std::nullopt;
  }
  AtlasInfo& info = it->second;
  if (!info.dynamic) {
    JM_LOG_ERROR("[AtlasManager] addRegion: atlas is static (loadAtlas-populated); "
                 "addRegion only operates on dynamic atlases from createDynamicAtlas");
    return std::nullopt;
  }
  if (pixels == nullptr || width == 0 || height == 0) {
    JM_LOG_ERROR("[AtlasManager] addRegion: invalid pixel data or dimensions");
    return std::nullopt;
  }
  if (width > info.width || height > info.height) {
    JM_LOG_ERROR("[AtlasManager] addRegion: region {}x{} exceeds atlas {}x{}",
                 width, height, info.width, info.height);
    return std::nullopt;
  }
  const std::string nameKey(name);
  if (info.regions.contains(nameKey)) {
    JM_LOG_ERROR("[AtlasManager] addRegion: name '{}' already exists in atlas", nameKey);
    return std::nullopt;
  }

  jm::atlas::ShelfPackerState state{
      info.width, info.height,
      info.shelfTop, info.shelfHeight, info.shelfCursor,
  };
  auto slot = jm::atlas::packShelf(state, width, height);
  if (!slot.has_value()) {
    JM_LOG_WARN("[AtlasManager] addRegion: atlas full (cannot fit {}x{})",
                width, height);
    return std::nullopt;
  }
  info.shelfTop = state.shelfTop;
  info.shelfHeight = state.shelfHeight;
  info.shelfCursor = state.shelfCursor;

  if (!renderer.subUploadTexture(info.texture,
                                 static_cast<int>(slot->x), static_cast<int>(slot->y),
                                 static_cast<int>(width), static_cast<int>(height),
                                 pixels)) {
    JM_LOG_ERROR("[AtlasManager] addRegion: subUploadTexture failed");
    return std::nullopt;
  }

  const float u = static_cast<float>(slot->x) / static_cast<float>(info.width);
  const float v = static_cast<float>(slot->y) / static_cast<float>(info.height);
  const float uw = static_cast<float>(width) / static_cast<float>(info.width);
  const float vh = static_cast<float>(height) / static_cast<float>(info.height);
  glm::vec4 uvRect(u, v, uw, vh);
  info.regions.emplace(nameKey, uvRect);
  return uvRect;
}
