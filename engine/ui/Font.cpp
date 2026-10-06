#include "Font.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include <algorithm>
#include <string>

#include "../core/logger/logging.hpp"

namespace {

// Why `bytes` can't be handed to stb_truetype, which trusts every offset in a
// font; empty if the parts it reads up front are all inside the buffer: the
// table directory, the fixed-size tables and the cmap/hmtx/loca extents.
// (Glyph outlines are read later, unchecked: fonts are the game's own assets.)
std::string sfntProblem(const std::vector<uint8_t>& bytes) {
  const uint64_t size = bytes.size();
  auto u16 = [&](uint64_t at) { return static_cast<uint32_t>(bytes[at] << 8 | bytes[at + 1]); };
  auto u32 = [&](uint64_t at) { return u16(at) << 16 | u16(at + 2); };
  if (size < 12) return "too short for a font";
  const uint32_t version = u32(0);
  if (version != 0x00010000 && version != 0x74727565 /* true */ && version != 0x4F54544F /* OTTO */) {
    return "not a TrueType/OpenType font";
  }
  const uint32_t numTables = u16(4);
  if (12 + 16 * uint64_t(numTables) > size) return "table directory past the end";

  struct Table {
    uint64_t offset = 0, length = 0;
    bool present = false;
  };
  auto table = [&](const char* tag) {
    for (uint32_t i = 0; i < numTables; ++i) {
      const uint64_t entry = 12 + 16 * uint64_t(i);
      if (std::equal(tag, tag + 4, bytes.begin() + static_cast<std::ptrdiff_t>(entry))) {
        return Table{u32(entry + 8), u32(entry + 12), true};
      }
    }
    return Table{};
  };
  for (uint32_t i = 0; i < numTables; ++i) {
    const uint64_t entry = 12 + 16 * uint64_t(i);
    if (uint64_t(u32(entry + 8)) + u32(entry + 12) > size) return "a table runs past the end";
  }

  const Table head = table("head"), hhea = table("hhea"), hmtx = table("hmtx"), cmap = table("cmap");
  const Table maxp = table("maxp"), loca = table("loca"), glyf = table("glyf"), cff = table("CFF ");
  if (!head.present || !hhea.present || !hmtx.present || !cmap.present) return "missing a required table";
  if (head.length < 54 || hhea.length < 36 || cmap.length < 4) return "a table is too short";
  if (uint64_t(u16(hhea.offset + 34)) * 4 > hmtx.length) return "hmtx shorter than hhea says";
  const uint32_t subtables = u16(cmap.offset + 2);
  if (4 + 8 * uint64_t(subtables) > cmap.length) return "cmap directory past its table";
  for (uint32_t i = 0; i < subtables; ++i) {
    if (uint64_t(u32(cmap.offset + 4 + 8 * uint64_t(i) + 4)) + 4 > cmap.length) return "cmap subtable past its table";
  }
  if (glyf.present) {
    if (!loca.present || !maxp.present || maxp.length < 6) return "glyf without loca/maxp";
    const uint64_t entries = uint64_t(u16(maxp.offset + 4)) + 1;
    if (entries * (u16(head.offset + 50) ? 4 : 2) > loca.length) return "loca shorter than maxp says";
  } else if (!cff.present) {
    return "no glyph outlines (glyf or CFF)";
  }
  return {};
}

}  // namespace

std::unique_ptr<Font> Font::tryLoad(std::vector<uint8_t> bytes) {
  if (bytes.empty()) {
    JM_LOG_ERROR("[Font] tryLoad: empty buffer");
    return nullptr;
  }
  if (const std::string problem = sfntProblem(bytes); !problem.empty()) {
    JM_LOG_ERROR("[Font] tryLoad: {}", problem);
    return nullptr;
  }
  std::unique_ptr<Font> font(new Font());
  font->_bytes = std::move(bytes);
  font->_info = std::make_unique<stbtt_fontinfo>();
  // Font index 0 (.ttc collections are rejected by the converter).
  if (!stbtt_InitFont(font->_info.get(), font->_bytes.data(), 0)) return nullptr;

  FontMetrics& m = font->_metrics;
  stbtt_GetFontVMetrics(font->_info.get(), &m.ascent, &m.descent, &m.lineGap);
  // ScaleForMappingEmToPixels(1) == 1 / unitsPerEm (ScaleForPixelHeight would
  // divide by ascent - descent instead).
  const float invUpem = stbtt_ScaleForMappingEmToPixels(font->_info.get(), 1.0f);
  m.unitsPerEm = invUpem > 0.0f ? static_cast<int>(1.0f / invUpem + 0.5f) : 0;
  if (m.unitsPerEm <= 0) {
    JM_LOG_ERROR("[Font] tryLoad: non-positive unitsPerEm ({})", m.unitsPerEm);
    return nullptr;
  }
  return font;
}

Font::~Font() = default;

float Font::scaleFor(float px) const {
  return stbtt_ScaleForMappingEmToPixels(_info.get(), px);
}

float Font::advanceUnits(uint32_t codepoint) const {
  int advance = 0, lsb = 0;
  stbtt_GetCodepointHMetrics(_info.get(), static_cast<int>(codepoint), &advance, &lsb);
  return static_cast<float>(advance);
}

float Font::kernUnits(uint32_t left, uint32_t right) const {
  return static_cast<float>(stbtt_GetCodepointKernAdvance(_info.get(), static_cast<int>(left), static_cast<int>(right)));
}

Font::Bitmap Font::rasterize(uint32_t codepoint, float scale, bool crisp) const {
  Bitmap b;
  int x0, y0, x1, y1;
  stbtt_GetCodepointBitmapBox(_info.get(), static_cast<int>(codepoint), scale, scale, &x0, &y0, &x1, &y1);
  b.width = std::max(0, x1 - x0);
  b.height = std::max(0, y1 - y0);
  b.xoff = x0;
  b.yoff = y0;
  if (b.width == 0 || b.height == 0) return b;
  b.alpha.resize(static_cast<size_t>(b.width) * b.height);
  stbtt_MakeCodepointBitmap(_info.get(), b.alpha.data(), b.width, b.height, b.width, scale, scale, static_cast<int>(codepoint));
  if (crisp) {
    for (auto& a : b.alpha) a = a >= 128 ? 255 : 0;
  }
  return b;
}
