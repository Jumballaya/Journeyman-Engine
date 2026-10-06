#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "../Font.hpp"
#include "../FontHandle.hpp"
#include "../FontRegistry.hpp"

// Tests build their font in memory: the seven tables stbtt_InitFont needs
// (cmap, glyf, head, hhea, hmtx, loca, maxp) with one empty glyph, and real
// unitsPerEm / ascent / descent so the metrics are actually parsed.
namespace {

// Big-endian writes: `put` at an offset, `push` at the end.
void put(std::vector<uint8_t>& b, size_t pos, uint32_t v, int bytes) {
  for (int i = 0; i < bytes; ++i) b[pos + i] = static_cast<uint8_t>(v >> (8 * (bytes - 1 - i)));
}
void push(std::vector<uint8_t>& b, uint32_t v, int bytes) {
  b.resize(b.size() + bytes);
  put(b, b.size() - bytes, v, bytes);
}

// Generate a minimal valid TrueType font. ascent>0, descent<0, unitsPerEm>0 so
// the metrics assertions are meaningful.
std::vector<uint8_t> makeMinimalTTF(uint16_t unitsPerEm = 1000,
                                    int16_t ascent = 800,
                                    int16_t descent = -200,
                                    int16_t lineGap = 90) {
  struct Table {
    char tag[4];
    std::vector<uint8_t> data;
  };

  // head: 54 bytes. unitsPerEm @18, indexToLocFormat @50 (0 = short loca).
  std::vector<uint8_t> head(54, 0);
  put(head, 0, 0x00010000, 4);   // version 1.0
  put(head, 12, 0x5F0F3CF5, 4);  // magicNumber
  put(head, 18, unitsPerEm, 2);
  put(head, 50, 0, 2);  // indexToLocFormat: short offsets

  // hhea: 36 bytes. ascender @4, descender @6, lineGap @8, numberOfHMetrics @34.
  std::vector<uint8_t> hhea(36, 0);
  put(hhea, 0, 0x00010000, 4);
  put(hhea, 4, ascent, 2);
  put(hhea, 6, descent, 2);
  put(hhea, 8, lineGap, 2);
  put(hhea, 34, 1, 2);  // numberOfHMetrics

  // maxp: 6-byte v0.5. numGlyphs @4.
  std::vector<uint8_t> maxp(6, 0);
  put(maxp, 0, 0x00005000, 4);  // version 0.5
  put(maxp, 4, 1, 2);           // numGlyphs = 1 (.notdef only)

  // hmtx: numberOfHMetrics (=1) longHorMetric: advanceWidth + lsb.
  std::vector<uint8_t> hmtx(4, 0);
  put(hmtx, 0, 500, 2);  // advanceWidth
  put(hmtx, 2, 0, 2);    // leftSideBearing

  // loca (short): numGlyphs+1 = 2 entries, both 0 → glyph 0 is empty.
  std::vector<uint8_t> loca(4, 0);

  // glyf: empty (glyph 0 has zero length).
  std::vector<uint8_t> glyf;

  // cmap: stb_truetype's InitFont REQUIRES a usable encoding subtable
  // (it returns 0 if info->index_map stays 0). Provide one Microsoft/Unicode-BMP
  // (platform 3, encoding 1) record pointing at a format-0 subtable.
  std::vector<uint8_t> cmap;
  push(cmap, 0, 2);   // version
  push(cmap, 1, 2);   // numTables
  push(cmap, 3, 2);   // platformID = Microsoft
  push(cmap, 1, 2);   // encodingID = Unicode BMP
  push(cmap, 12, 4);  // offset to subtable (4-byte header + 8-byte record)
  push(cmap, 0, 2);    // subtable format 0 (byte encoding table)
  push(cmap, 262, 2);  // length (6 header + 256 glyph indices)
  push(cmap, 0, 2);    // language
  for (int i = 0; i < 256; ++i) cmap.push_back(0);  // glyphIdArray → glyph 0

  std::vector<Table> tables = {
      {{'c', 'm', 'a', 'p'}, cmap}, {{'g', 'l', 'y', 'f'}, glyf},
      {{'h', 'e', 'a', 'd'}, head}, {{'h', 'h', 'e', 'a'}, hhea},
      {{'h', 'm', 't', 'x'}, hmtx}, {{'l', 'o', 'c', 'a'}, loca},
      {{'m', 'a', 'x', 'p'}, maxp},
  };

  const uint16_t numTables = static_cast<uint16_t>(tables.size());
  const size_t dirSize = 12 + numTables * 16;

  std::vector<uint8_t> out;
  // Offset table (sfnt header). searchRange/entrySelector/rangeShift are not
  // validated by stb_truetype, so leave them zero.
  push(out, 0x00010000, 4);  // sfntVersion: TrueType outlines
  push(out, numTables, 2);
  push(out, 0, 2);  // searchRange
  push(out, 0, 2);  // entrySelector
  push(out, 0, 2);  // rangeShift

  // Reserve directory space; fill offsets/lengths as we append table data.
  out.resize(dirSize, 0);
  size_t cursor = dirSize;
  for (size_t i = 0; i < tables.size(); ++i) {
    const size_t dirEntry = 12 + i * 16;
    std::copy(tables[i].tag, tables[i].tag + 4, out.begin() + dirEntry);
    put(out, dirEntry + 4, 0, 4);  // checkSum (ignored by stb_truetype)
    put(out, dirEntry + 8, static_cast<uint32_t>(cursor), 4);
    put(out, dirEntry + 12, static_cast<uint32_t>(tables[i].data.size()), 4);

    out.insert(out.end(), tables[i].data.begin(), tables[i].data.end());
    cursor += tables[i].data.size();
    // 4-byte align the next table (spec-conformant; stb_truetype is lenient).
    for (; cursor % 4 != 0; ++cursor) out.push_back(0);
  }

  return out;
}

}  // namespace

// 1. Happy path — ttf load + register.
TEST(FontRegistry, HappyPathTtfLoad) {
  auto font = Font::tryLoad(makeMinimalTTF());
  ASSERT_NE(font, nullptr);

  FontRegistry reg;
  const FontHandle handle = reg.registerFont("fonts/test.ttf", std::move(font));

  EXPECT_TRUE(handle.isValid());
  ASSERT_NE(reg.getFont(handle), nullptr);
  EXPECT_GT(reg.getFont(handle)->metrics().ascent, 0);
  EXPECT_LT(reg.getFont(handle)->metrics().descent, 0);
  EXPECT_GT(reg.getFont(handle)->metrics().unitsPerEm, 0);
}

// 2. An .otf path registers and resolves like a .ttf one.
TEST(FontRegistry, OtfPathLoadAndLookup) {
  auto font = Font::tryLoad(makeMinimalTTF());
  ASSERT_NE(font, nullptr);

  FontRegistry reg;
  const FontHandle handle = reg.registerFont("fonts/test.otf", std::move(font));

  EXPECT_TRUE(handle.isValid());
  EXPECT_NE(reg.getFont(reg.handleForPath("fonts/test.otf")), nullptr);
  EXPECT_EQ(reg.handleForPath("fonts/test.otf"), handle);
}

// 3. Lookup-by-handle, unknown handle.
TEST(FontRegistry, UnknownHandle) {
  FontRegistry reg;
  EXPECT_EQ(reg.getFont(FontHandle{99999}), nullptr);
}

// 4. Path canonicalization: "./fonts/x.ttf" and "fonts/x.ttf" resolve to the
// same Font* and the same handle.
TEST(FontRegistry, PathCanonicalization) {
  auto font = Font::tryLoad(makeMinimalTTF());
  ASSERT_NE(font, nullptr);

  FontRegistry reg;
  const FontHandle handle = reg.registerFont("./fonts/x.ttf", std::move(font));

  const Font* a = reg.getFont(reg.handleForPath("fonts/x.ttf"));
  const Font* b = reg.getFont(reg.handleForPath("./fonts/x.ttf"));
  ASSERT_NE(a, nullptr);
  EXPECT_EQ(a, b);
  EXPECT_EQ(reg.handleForPath("fonts/x.ttf"), handle);
  EXPECT_EQ(reg.handleForPath("./fonts/x.ttf"), handle);
}

// 5. Malformed (non-font) bytes are rejected.
TEST(FontRegistry, MalformedBytesRejected) {
  EXPECT_EQ(Font::tryLoad(std::vector<uint8_t>{1, 2, 3, 4, 5}), nullptr);
}

// 6. Empty buffer rejected by the early-return guard.
TEST(FontRegistry, EmptyBufferRejected) {
  EXPECT_EQ(Font::tryLoad(std::vector<uint8_t>{}), nullptr);
}

// 7. Metrics survive registry storage unchanged (catches slicing / rebuild).
TEST(FontRegistry, MetricsSurviveStorage) {
  auto font = Font::tryLoad(makeMinimalTTF(/*unitsPerEm=*/2048, /*ascent=*/1536, /*descent=*/-512));
  ASSERT_NE(font, nullptr);
  const FontMetrics expected = font->metrics();

  FontRegistry reg;
  const FontHandle handle = reg.registerFont("fonts/m.ttf", std::move(font));

  const Font* stored = reg.getFont(handle);
  ASSERT_NE(stored, nullptr);
  EXPECT_EQ(stored->metrics().ascent, expected.ascent);
  EXPECT_EQ(stored->metrics().descent, expected.descent);
  EXPECT_EQ(stored->metrics().lineGap, expected.lineGap);
  EXPECT_EQ(stored->metrics().unitsPerEm, expected.unitsPerEm);
  EXPECT_EQ(stored->metrics().unitsPerEm, 2048);
}

// 8. Re-registering the same path reuses the handle and overwrites the font
// (hot-reload semantics).
TEST(FontRegistry, ReRegisterSamePathReusesHandle) {
  FontRegistry reg;
  const FontHandle h1 = reg.registerFont("fonts/r.ttf", Font::tryLoad(makeMinimalTTF()));
  const FontHandle h2 = reg.registerFont("./fonts/r.ttf", Font::tryLoad(makeMinimalTTF()));

  EXPECT_EQ(h1, h2);
  EXPECT_EQ(reg.fontCount(), 1u);
}
