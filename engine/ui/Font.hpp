#pragma once

#include <cstdint>
#include <memory>
#include <vector>

struct stbtt_fontinfo;  // forward decl — opaque; defined in stb_truetype.h.
                        // PIMPL-lite: only Font.cpp includes stb_truetype.h.

struct FontMetrics {
  int ascent = 0;       // unscaled, in font design units.
  int descent = 0;      // unscaled, negative per stb convention.
  int lineGap = 0;      // unscaled.
  int unitsPerEm = 0;   // design units per em; G.2 scales by this per pixel size.
};

// Font wraps stb_truetype's parsed fontinfo plus the retained ttf/otf byte
// buffer. stb_truetype reads glyph outlines lazily out of the source bytes, so
// the buffer MUST outlive the fontinfo — see the member declaration order note
// below. Main-thread only (mirrors AtlasManager's threading contract).
class Font {
 public:
  // Factory. Takes ownership of the ttf/otf byte buffer. Returns nullptr if the
  // buffer is empty, stbtt_InitFont fails (corrupt / non-font bytes), or the
  // parsed unitsPerEm is non-positive (guards G.2's per-size scale math).
  static std::unique_ptr<Font> tryLoad(std::vector<uint8_t> bytes);

  ~Font();
  Font(const Font&) = delete;
  Font& operator=(const Font&) = delete;

  const FontMetrics& metrics() const noexcept { return _metrics; }

  // Scale factor from design units to pixels for a font-size (em) of `px`.
  float scaleFor(float px) const;
  // Horizontal advance of `codepoint` in design units (scale to pixels).
  float advanceUnits(uint32_t codepoint) const;
  float kernUnits(uint32_t left, uint32_t right) const;

  struct Bitmap {
    int width = 0, height = 0;
    int xoff = 0, yoff = 0;  // top-left offset from the pen at the baseline
    std::vector<uint8_t> alpha;
  };
  // Rasterizes one glyph at `scale` (from scaleFor). `crisp` thresholds the
  // coverage to 0/255 for pixel fonts.
  Bitmap rasterize(uint32_t codepoint, float scale, bool crisp) const;
  const stbtt_fontinfo* info() const noexcept { return _info; }
  const std::vector<uint8_t>& bytes() const noexcept { return _bytes; }

 private:
  Font();  // tryLoad allocates + initializes.

  // Declaration order is load-bearing: C++ destroys members in reverse
  // declaration order, so _info is freed BEFORE _bytes. stb_truetype's
  // fontinfo points into _bytes, so the buffer must outlive the fontinfo.
  std::vector<uint8_t> _bytes;      // owns ttf/otf source bytes.
  stbtt_fontinfo* _info = nullptr;  // heap-allocated; deleted in dtor.
  FontMetrics _metrics{};
};
