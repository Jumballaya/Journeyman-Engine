#include "Font.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "../core/logger/logging.hpp"

std::unique_ptr<Font> Font::tryLoad(std::vector<uint8_t> bytes) {
  if (bytes.empty()) {
    JM_LOG_ERROR("[Font] tryLoad: empty buffer");
    return nullptr;
  }

  std::unique_ptr<Font> font(new Font());
  font->_bytes = std::move(bytes);
  font->_info = new stbtt_fontinfo();

  // stbtt_InitFont reads glyph data lazily out of the buffer — _bytes must
  // outlive _info. Member declaration order in Font.hpp guarantees that on
  // destroy. Font index 0; .ttc collections are rejected upstream (converter).
  if (!stbtt_InitFont(font->_info, font->_bytes.data(), 0)) {
    return nullptr;  // dtor frees _info + _bytes.
  }

  stbtt_GetFontVMetrics(font->_info,
                        &font->_metrics.ascent,
                        &font->_metrics.descent,
                        &font->_metrics.lineGap);

  // unitsPerEm: stbtt_ScaleForMappingEmToPixels(info, 1.0f) returns
  // 1.0f / unitsPerEm (it reads the head table's unitsPerEm directly). Invert
  // to recover the integer. NOT stbtt_ScaleForPixelHeight, which divides by
  // (ascent - descent) — a different denominator.
  const float invUpem = stbtt_ScaleForMappingEmToPixels(font->_info, 1.0f);
  font->_metrics.unitsPerEm =
      (invUpem > 0.0f) ? static_cast<int>(1.0f / invUpem + 0.5f) : 0;

  if (font->_metrics.unitsPerEm <= 0) {
    JM_LOG_ERROR("[Font] tryLoad: non-positive unitsPerEm ({})",
                 font->_metrics.unitsPerEm);
    return nullptr;  // dtor frees _info + _bytes.
  }

  return font;
}

Font::Font() = default;

Font::~Font() {
  delete _info;  // safe on nullptr.
  // _bytes destructed automatically AFTER _info (reverse member-decl order).
}
