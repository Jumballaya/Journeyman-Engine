#include "GlyphCache.hpp"

#include <string>

#include "../core/logger/logging.hpp"

const GlyphCache::Glyph& GlyphCache::get(const Font& font, FontHandle fontHandle, uint32_t rasterPx, bool crisp,
                                         uint32_t codepoint) {
  const Key key{fontHandle.id, rasterPx, codepoint, crisp};
  if (auto it = _glyphs.find(key); it != _glyphs.end()) return it->second;

  Glyph glyph;
  Font::Bitmap bmp = font.rasterize(codepoint, font.scaleFor(static_cast<float>(rasterPx)), crisp);
  if (!bmp.alpha.empty()) {
    // 1px transparent border so linear filtering never bleeds neighbors in.
    const int w = bmp.width + 2, h = bmp.height + 2;
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4, 0);
    for (int y = 0; y < bmp.height; ++y) {
      for (int x = 0; x < bmp.width; ++x) {
        uint8_t* p = &rgba[(static_cast<size_t>(y + 1) * w + (x + 1)) * 4];
        p[0] = p[1] = p[2] = 255;
        p[3] = bmp.alpha[static_cast<size_t>(y) * bmp.width + x];
      }
    }
    const std::string name = std::to_string(fontHandle.id) + ":" + std::to_string(rasterPx) + ":" +
                             std::to_string(codepoint) + (crisp ? "c" : "s");
    if (auto packed = pack(name, rgba, w, h, crisp)) {
      glyph.texture = packed->first;
      glyph.uv = packed->second;
      glyph.offset = glm::vec2(static_cast<float>(bmp.xoff - 1), static_cast<float>(bmp.yoff - 1));
      glyph.size = glm::vec2(static_cast<float>(w), static_cast<float>(h));
    }
  }
  return _glyphs.emplace(key, glyph).first->second;
}

std::optional<std::pair<TextureHandle, glm::vec4>> GlyphCache::pack(const std::string& name,
                                                                    const std::vector<uint8_t>& rgba, int w, int h,
                                                                    bool crisp) {
  auto& pages = _pages[crisp ? 1 : 0];
  // Try the newest page first; open a new page when it's full.
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (!pages.empty()) {
      if (auto uv = _atlases.addRegion(_renderer, pages.back(), name, rgba.data(), w, h)) {
        auto found = _atlases.lookup(pages.back(), name);
        return std::make_pair(found->first, *uv);
      }
    }
    AssetHandle page = _atlases.createDynamicAtlas(_assets, _renderer, kPageSize, kPageSize, crisp ? "nearest" : "linear");
    if (!page.isValid()) return std::nullopt;
    pages.push_back(page);
  }
  JM_LOG_ERROR("[UI] glyph {}x{} does not fit in an empty {}px page", w, h, kPageSize);
  return std::nullopt;
}
