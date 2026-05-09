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

// AtlasManager: in-memory registry of texture atlases, keyed by the
// AssetHandle the AssetManager issued for the .atlas.json. Each atlas owns a
// table of named regions stored as normalized UV rects (post pixel→UV
// conversion done at loadAtlas time). The renderer consumes this via
// SpriteComponent's deserializer (F.3) which produces (TextureHandle, texRect)
// from a `texture#region` reference.
//
// Lifetime: owned by Renderer2DModule, parallel to its AssetRegistry<TextureHandle>
// member. Atlases are not unloaded during a scene's lifetime today; F-next may
// add an unload path (driven by AssetManager hot-reload or scene boundaries).
class AtlasManager {
 public:
  // Register an atlas. Pixel regions are converted to normalized UVs once,
  // here. `sourcePath` is the .atlas.json's manifest-root-relative path; it is
  // canonicalized (via lexically_normal().generic_string()) for path-keyed
  // lookups via lookupByPath. `texture` is the GPU handle of the packed
  // image (already loaded — caller is responsible for ordering).
  //
  // Re-registering the same handle overwrites the prior entry (rare, but
  // safe — covers a hypothetical hot-reload path).
  void loadAtlas(AssetHandle handle,
                 const std::filesystem::path& sourcePath,
                 TextureHandle texture,
                 uint32_t width, uint32_t height,
                 const std::unordered_map<std::string, std::array<int, 4>>& pixelRegions);

  // Allocate an empty atlas backed by a fresh GPU texture. Returns a
  // synthetic AssetHandle (no source path). Caller fills regions via
  // addRegion. filter is "nearest" or "linear"; defaults to "nearest" for
  // any other value. width/height must be > 0 and within GL_MAX_TEXTURE_SIZE
  // (caller is responsible — typical dynamic atlases are 256–2048 per side).
  // Main-thread-only. Templated on Renderer so unit tests can pass a
  // FakeRenderer2D stub without bringing up GL; production passes Renderer2D.
  // Body must live in this header (template definition rule).
  template <typename Renderer>
  AssetHandle createDynamicAtlas(AssetManager& assets, Renderer& renderer,
                                 uint32_t width, uint32_t height,
                                 std::string_view filter);

  // Pack a (w, h) RGBA8 region into a previously-created dynamic atlas.
  // Returns the assigned UV rect on success. Returns nullopt if:
  //   - the atlas handle is unknown or refers to a static (loadAtlas) atlas
  //   - the atlas is full (region doesn't fit even on a new shelf)
  //   - the name collides with an existing region in this atlas
  //   - pixels is null, w/h <= 0, or w/h > atlas dimensions
  // On success: glTexSubImage2D-uploads pixels at the assigned slot AND
  // stores the UV rect in AtlasInfo.regions for subsequent lookups.
  // Main-thread-only. Templated on Renderer (see createDynamicAtlas above);
  // body lives in this header.
  template <typename Renderer>
  std::optional<glm::vec4>
  addRegion(Renderer& renderer, AssetHandle atlasHandle,
            std::string_view name, const void* pixels,
            uint32_t width, uint32_t height);

  // Handle-keyed lookup. Returns nullopt if the handle is unknown OR the
  // region name is not in this atlas's table.
  std::optional<std::pair<TextureHandle, glm::vec4>>
  lookup(AssetHandle atlasHandle, std::string_view region) const;

  // Path-keyed lookup. Canonicalizes `atlasPath` the same way loadAtlas did,
  // resolves to a handle, and forwards to lookup. Returns nullopt if no atlas
  // is registered at that path. Scene deserializer (F.3) is the primary
  // caller: it has the path string from JSON and doesn't carry the handle.
  std::optional<std::pair<TextureHandle, glm::vec4>>
  lookupByPath(std::string_view atlasPath, std::string_view region) const;

  size_t atlasCount() const noexcept { return _atlases.size(); }
  bool hasAtlas(AssetHandle handle) const noexcept { return _atlases.find(handle) != _atlases.end(); }

 private:
  struct AtlasInfo {
    TextureHandle texture;
    uint32_t width = 0;
    uint32_t height = 0;
    // Region rects in NORMALIZED UV space ([u, v, w, h]). Pixel→UV happens
    // once at loadAtlas; lookup is a straight map fetch.
    std::unordered_map<std::string, glm::vec4> regions;

    // Shelf-pack state — used by addRegion for dynamic atlases. For static
    // atlases populated via loadAtlas these are zero and never read.
    bool dynamic = false;
    uint32_t shelfTop = 0;
    uint32_t shelfHeight = 0;
    uint32_t shelfCursor = 0;
  };

  std::unordered_map<AssetHandle, AtlasInfo> _atlases;
  // canonical path → handle. Built alongside _atlases at loadAtlas time.
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
