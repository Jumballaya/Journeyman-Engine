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

void GlyphCache::beginFrame() {
  if (!_overBudget) return;
  _overBudget = false;
  _glyphs.clear();
  for (int kind = 0; kind < 2; ++kind) {
    auto& pages = _pages[kind];
    while (pages.size() > kMaxPages) {
      _gpu.release(_atlases.removeAtlas(pages.back()));
      pages.pop_back();
    }
    for (AssetHandle page : pages) _atlases.clearDynamicAtlas(page);
    _openPage[kind] = 0;
  }
  JM_LOG_INFO("[UI] glyph cache started over (more than {} pages of glyphs)", kMaxPages);
}

std::optional<std::pair<TextureHandle, glm::vec4>> GlyphCache::pack(const std::string& name,
                                                                    const std::vector<uint8_t>& rgba, int w, int h,
                                                                    bool crisp) {
  if (w > static_cast<int>(kPageSize) || h > static_cast<int>(kPageSize)) {
    JM_LOG_ERROR("[UI] glyph {}x{} is larger than a {}px atlas page; not drawn", w, h, kPageSize);
    return std::nullopt;
  }
  const int kind = crisp ? 1 : 0;
  auto& pages = _pages[kind];
  size_t& open = _openPage[kind];
  // Fill the open page, then the next (pages kept from before a start-over),
  // then a new one: past the budget, this frame still gets it, and the next
  // frame starts over.
  for (;; ++open) {
    if (open == pages.size()) {
      if (pages.size() >= kMaxPages) _overBudget = true;
      AssetHandle page = _atlases.createDynamicAtlas(_assets, _gpu, kPageSize, kPageSize, crisp ? "nearest" : "linear");
      if (!page.isValid()) return std::nullopt;
      pages.push_back(page);
      if (auto uv = _atlases.addRegion(_gpu, page, name, rgba.data(), w, h)) {
        return std::make_pair(_atlases.lookup(page, name)->first, *uv);
      }
      break;  // doesn't fit even an empty page
    }
    if (auto uv = _atlases.addRegion(_gpu, pages[open], name, rgba.data(), w, h)) {
      return std::make_pair(_atlases.lookup(pages[open], name)->first, *uv);
    }
  }
  JM_LOG_ERROR("[UI] glyph {}x{} does not fit in an empty {}px page", w, h, kPageSize);
  return std::nullopt;
}
