#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "../core/assets/AssetHandle.hpp"
#include "../core/assets/AssetManager.hpp"
#include "../renderer2d/AtlasManager.hpp"
#include "../renderer2d/Renderer2D.hpp"
#include "Font.hpp"
#include "FontHandle.hpp"

// Rasterized glyphs packed into dynamic atlas pages (AtlasManager). Glyphs
// are rasterized at the on-screen pixel size (font-size × renderer pixel
// scale) so text stays sharp at any window size. Main thread only.
class GlyphCache {
 public:
  struct Glyph {
    TextureHandle texture;     // invalid for blank glyphs (space)
    glm::vec4 uv{0.0f};
    glm::vec2 offset{0.0f};    // raster px, top-left relative to pen on baseline
    glm::vec2 size{0.0f};      // raster px
  };

  GlyphCache(AtlasManager& atlases, AssetManager& assets, Renderer2D& renderer)
      : _atlases(atlases), _assets(assets), _renderer(renderer) {}

  const Glyph& get(const Font& font, FontHandle fontHandle, uint32_t rasterPx, bool crisp, uint32_t codepoint);

 private:
  static constexpr uint32_t kPageSize = 1024;

  struct Key {
    uint32_t font, px, codepoint;
    bool crisp;
    bool operator==(const Key&) const = default;
  };
  struct KeyHash {
    size_t operator()(const Key& k) const noexcept {
      return (static_cast<size_t>(k.font) * 73856093u) ^ (static_cast<size_t>(k.px) * 19349663u) ^
             (static_cast<size_t>(k.codepoint) * 83492791u) ^ (k.crisp ? 0x9e3779b9u : 0u);
    }
  };

  std::optional<std::pair<TextureHandle, glm::vec4>> pack(const std::string& name, const std::vector<uint8_t>& rgba,
                                                          int w, int h, bool crisp);

  AtlasManager& _atlases;
  AssetManager& _assets;
  Renderer2D& _renderer;
  std::unordered_map<Key, Glyph, KeyHash> _glyphs;
  std::vector<AssetHandle> _pages[2];  // [0] smooth (linear), [1] crisp (nearest)
};
