#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "../Font.hpp"
#include "../FontHandle.hpp"
#include "../FontRegistry.hpp"

// ---------------------------------------------------------------------------
// Ad-hoc test setup (G.1).
//
// G.1 ships BEFORE G.5's UITestFixture, so these tests construct their inputs
// directly: a programmatically generated minimal-but-valid TrueType font. This
// keeps the suite hermetic (no committed binary fixture, no network, no system
// font dependency, cross-platform). From G.5 onward the locked UITestFixture is
// canonical; UI tests inherit from it rather than reinventing setup.
//
// The generator below emits the seven tables stbtt_InitFont requires to accept
// a TrueType face (cmap, glyf, head, hhea, hmtx, loca, maxp) with a single
// empty glyph. Real unitsPerEm / ascent / descent live in head / hhea so the
// metrics assertions exercise actual parsing, not placeholder zeros.
// ---------------------------------------------------------------------------
namespace {

void writeU16(std::vector<uint8_t>& b, size_t pos, uint16_t v) {
  b[pos] = static_cast<uint8_t>((v >> 8) & 0xFF);
  b[pos + 1] = static_cast<uint8_t>(v & 0xFF);
}
void writeI16(std::vector<uint8_t>& b, size_t pos, int16_t v) {
  writeU16(b, pos, static_cast<uint16_t>(v));
}
void writeU32(std::vector<uint8_t>& b, size_t pos, uint32_t v) {
  b[pos] = static_cast<uint8_t>((v >> 24) & 0xFF);
  b[pos + 1] = static_cast<uint8_t>((v >> 16) & 0xFF);
  b[pos + 2] = static_cast<uint8_t>((v >> 8) & 0xFF);
  b[pos + 3] = static_cast<uint8_t>(v & 0xFF);
}
void pushU16(std::vector<uint8_t>& b, uint16_t v) {
  b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  b.push_back(static_cast<uint8_t>(v & 0xFF));
}
void pushU32(std::vector<uint8_t>& b, uint32_t v) {
  b.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
  b.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
  b.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
  b.push_back(static_cast<uint8_t>(v & 0xFF));
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
  writeU32(head, 0, 0x00010000);   // version 1.0
  writeU32(head, 12, 0x5F0F3CF5);  // magicNumber
  writeU16(head, 18, unitsPerEm);
  writeI16(head, 50, 0);  // indexToLocFormat: short offsets

  // hhea: 36 bytes. ascender @4, descender @6, lineGap @8, numberOfHMetrics @34.
  std::vector<uint8_t> hhea(36, 0);
  writeU32(hhea, 0, 0x00010000);
  writeI16(hhea, 4, ascent);
  writeI16(hhea, 6, descent);
  writeI16(hhea, 8, lineGap);
  writeU16(hhea, 34, 1);  // numberOfHMetrics

  // maxp: 6-byte v0.5. numGlyphs @4.
  std::vector<uint8_t> maxp(6, 0);
  writeU32(maxp, 0, 0x00005000);  // version 0.5
  writeU16(maxp, 4, 1);           // numGlyphs = 1 (.notdef only)

  // hmtx: numberOfHMetrics (=1) longHorMetric: advanceWidth + lsb.
  std::vector<uint8_t> hmtx(4, 0);
  writeU16(hmtx, 0, 500);  // advanceWidth
  writeI16(hmtx, 2, 0);    // leftSideBearing

  // loca (short): numGlyphs+1 = 2 entries, both 0 → glyph 0 is empty.
  std::vector<uint8_t> loca(4, 0);

  // glyf: empty (glyph 0 has zero length).
  std::vector<uint8_t> glyf;

  // cmap: stb_truetype's InitFont REQUIRES a usable encoding subtable
  // (it returns 0 if info->index_map stays 0). Provide one Microsoft/Unicode-BMP
  // (platform 3, encoding 1) record pointing at a format-0 subtable.
  std::vector<uint8_t> cmap;
  pushU16(cmap, 0);   // version
  pushU16(cmap, 1);   // numTables
  pushU16(cmap, 3);   // platformID = Microsoft
  pushU16(cmap, 1);   // encodingID = Unicode BMP
  pushU32(cmap, 12);  // offset to subtable (4-byte header + 8-byte record)
  pushU16(cmap, 0);    // subtable format 0 (byte encoding table)
  pushU16(cmap, 262);  // length (6 header + 256 glyph indices)
  pushU16(cmap, 0);    // language
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
  pushU32(out, 0x00010000);  // sfntVersion: TrueType outlines
  pushU16(out, numTables);
  pushU16(out, 0);  // searchRange
  pushU16(out, 0);  // entrySelector
  pushU16(out, 0);  // rangeShift

  // Reserve directory space; fill offsets/lengths as we append table data.
  out.resize(dirSize, 0);
  size_t cursor = dirSize;
  for (size_t i = 0; i < tables.size(); ++i) {
    const size_t dirEntry = 12 + i * 16;
    out[dirEntry + 0] = static_cast<uint8_t>(tables[i].tag[0]);
    out[dirEntry + 1] = static_cast<uint8_t>(tables[i].tag[1]);
    out[dirEntry + 2] = static_cast<uint8_t>(tables[i].tag[2]);
    out[dirEntry + 3] = static_cast<uint8_t>(tables[i].tag[3]);
    writeU32(out, dirEntry + 4, 0);  // checkSum (ignored by stb_truetype)
    writeU32(out, dirEntry + 8, static_cast<uint32_t>(cursor));
    writeU32(out, dirEntry + 12, static_cast<uint32_t>(tables[i].data.size()));

    out.insert(out.end(), tables[i].data.begin(), tables[i].data.end());
    cursor += tables[i].data.size();
    // 4-byte align the next table (spec-conformant; stb_truetype is lenient).
    while ((cursor % 4) != 0) {
      out.push_back(0);
      ++cursor;
    }
  }

  return out;
}

}  // namespace

// 1. Happy path — ttf load + register.
TEST(FontRegistry, HappyPathTtfLoad) {
  auto font = Font::tryLoad(makeMinimalTTF());
  ASSERT_NE(font, nullptr);

  FontRegistry reg;
  const FontHandle handle = reg.registerFont(AssetHandle{42}, "fonts/test.ttf", std::move(font));

  EXPECT_TRUE(handle.isValid());
  ASSERT_NE(reg.getFont(handle), nullptr);
  EXPECT_GT(reg.getFont(handle)->metrics().ascent, 0);
  EXPECT_LT(reg.getFont(handle)->metrics().descent, 0);
  EXPECT_GT(reg.getFont(handle)->metrics().unitsPerEm, 0);
}

// 2. otf-named bytes load identically (stb_truetype's InitFont accepts both;
// the converter registers the same lambda for {".ttf", ".otf"} — verified by
// inspection of UIModule.cpp and the debug-build smoke). Here we assert the
// registry handles an .otf path the same as a .ttf one.
TEST(FontRegistry, OtfPathLoadAndLookup) {
  auto font = Font::tryLoad(makeMinimalTTF());
  ASSERT_NE(font, nullptr);

  FontRegistry reg;
  const FontHandle handle = reg.registerFont(AssetHandle{7}, "fonts/test.otf", std::move(font));

  EXPECT_TRUE(handle.isValid());
  EXPECT_NE(reg.getFontByPath("fonts/test.otf"), nullptr);
  EXPECT_EQ(reg.handleForPath("fonts/test.otf"), handle);
}

// 3. Lookup-by-handle, unknown handle.
TEST(FontRegistry, UnknownHandle) {
  FontRegistry reg;
  EXPECT_EQ(reg.getFont(FontHandle{99999}), nullptr);
  EXPECT_FALSE(reg.hasFont(FontHandle{99999}));
}

// 4. Path canonicalization: "./fonts/x.ttf" and "fonts/x.ttf" resolve to the
// same Font* and the same handle.
TEST(FontRegistry, PathCanonicalization) {
  auto font = Font::tryLoad(makeMinimalTTF());
  ASSERT_NE(font, nullptr);

  FontRegistry reg;
  const FontHandle handle = reg.registerFont(AssetHandle{1}, "./fonts/x.ttf", std::move(font));

  const Font* a = reg.getFontByPath("fonts/x.ttf");
  const Font* b = reg.getFontByPath("./fonts/x.ttf");
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
  const FontHandle handle = reg.registerFont(AssetHandle{5}, "fonts/m.ttf", std::move(font));

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
  const FontHandle h1 = reg.registerFont(AssetHandle{1}, "fonts/r.ttf", Font::tryLoad(makeMinimalTTF()));
  const FontHandle h2 = reg.registerFont(AssetHandle{2}, "./fonts/r.ttf", Font::tryLoad(makeMinimalTTF()));

  EXPECT_EQ(h1, h2);
  EXPECT_EQ(reg.fontCount(), 1u);
}
