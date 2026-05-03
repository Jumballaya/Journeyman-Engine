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
  };

  std::unordered_map<AssetHandle, AtlasInfo> _atlases;
  // canonical path → handle. Built alongside _atlases at loadAtlas time.
  std::unordered_map<std::string, AssetHandle> _pathIndex;
};
