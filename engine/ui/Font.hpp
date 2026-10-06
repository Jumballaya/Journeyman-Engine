#pragma once

#include <cstdint>
#include <memory>
#include <vector>

struct stbtt_fontinfo;  // only Font.cpp includes stb_truetype.h

// Unscaled, in font design units (stb convention: descent is negative).
struct FontMetrics {
  int ascent = 0;
  int descent = 0;
  int lineGap = 0;
  int unitsPerEm = 0;
};

// A parsed ttf/otf (stb_truetype) plus its bytes, which stb reads lazily, so
// they must outlive the fontinfo. Main thread only.
class Font {
 public:
  // Takes the bytes; nullptr if they aren't a usable font.
  static std::unique_ptr<Font> tryLoad(std::vector<uint8_t> bytes);

  ~Font();
  Font(const Font&) = delete;
  Font& operator=(const Font&) = delete;

  const FontMetrics& metrics() const noexcept { return _metrics; }

  // Scale factor from design units to pixels for a font-size (em) of `px`.
  float scaleFor(float px) const;
  // Horizontal advance and kerning in design units (scale to pixels).
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

 private:
  Font() = default;

  std::vector<uint8_t> _bytes;  // declared first: _info points into it
  std::unique_ptr<stbtt_fontinfo> _info;
  FontMetrics _metrics{};
};
