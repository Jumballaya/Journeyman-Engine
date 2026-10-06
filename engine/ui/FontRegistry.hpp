#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Font.hpp"
#include "FontHandle.hpp"

// Loaded fonts by handle and by path. Fonts are boxed: stbtt_fontinfo points
// into the font's bytes, so they need stable addresses. Main thread only.
class FontRegistry {
 public:
  // Re-registering a path keeps its handle and replaces the font.
  FontHandle registerFont(const std::filesystem::path& sourcePath, std::unique_ptr<Font> font);

  // Null if unknown.
  const Font* getFont(FontHandle handle) const noexcept;
  // Paths are compared lexically normalized; invalid if unknown.
  FontHandle handleForPath(std::string_view path) const;

  size_t fontCount() const noexcept { return _fonts.size(); }

 private:
  std::unordered_map<FontHandle, std::unique_ptr<Font>> _fonts;
  std::unordered_map<std::string, FontHandle> _pathIndex;
  uint32_t _nextId = 1;
};
