#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "../core/assets/AssetHandle.hpp"
#include "../core/assets/AssetManager.hpp"
#include "../renderer2d/AtlasManager.hpp"
#include "../renderer2d/GpuResources.hpp"
#include "Font.hpp"
#include "FontHandle.hpp"

// Glyphs rasterized at on-screen pixel size (sharp at any window size), packed
// into dynamic atlas pages. Main thread only.
//
// Every new pixel size is a new set of glyphs (a camera zooming over world text
// asks for one each frame), so the pages are budgeted: once a frame needed more
// than kMaxPages of one kind, the next beginFrame() starts the cache over,
// reusing kMaxPages pages and freeing the rest. Never mid-frame: text already
// drawn this frame still points into its page.
class GlyphCache {
 public:
  struct Glyph {
    TextureHandle texture;     // invalid for blank glyphs (space)
    glm::vec4 uv{0.0f};
    glm::vec2 offset{0.0f};    // raster px, top-left relative to pen on baseline
    glm::vec2 size{0.0f};      // raster px
  };

  GlyphCache(AtlasManager& atlases, AssetManager& assets, GpuResources& gpu)
      : _atlases(atlases), _assets(assets), _gpu(gpu) {}

  const Glyph& get(const Font& font, FontHandle fontHandle, uint32_t rasterPx, bool crisp, uint32_t codepoint);
  // Before the frame's text: starts over if the last frame went over budget.
  void beginFrame();
  size_t pageCount() const { return _pages[0].size() + _pages[1].size(); }

  static constexpr uint32_t kPageSize = 1024;
  static constexpr size_t kMaxPages = 4;  // per filter: 4 MB each

 private:

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
  GpuResources& _gpu;
  std::unordered_map<Key, Glyph, KeyHash> _glyphs;
  std::vector<AssetHandle> _pages[2];  // [0] smooth (linear), [1] crisp (nearest)
  size_t _openPage[2] = {0, 0};        // where packing continues after a start-over
  bool _overBudget = false;
};
