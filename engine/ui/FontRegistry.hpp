#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "../core/assets/AssetHandle.hpp"
#include "Font.hpp"
#include "FontHandle.hpp"

// Loaded fonts by handle and by path. Fonts are boxed: stbtt_fontinfo points
// into the font's bytes, so they need stable addresses. Main thread only.
class FontRegistry {
 public:
  // Re-registering a path keeps its handle and replaces the font.
  // `assetHandle` is unused (reserved for an asset→font index).
  FontHandle registerFont(AssetHandle assetHandle,
                          const std::filesystem::path& sourcePath,
                          std::unique_ptr<Font> font);

  // Handle-keyed lookup. Returns nullptr if unknown.
  const Font* getFont(FontHandle handle) const noexcept;

  // Path-keyed lookup. Canonicalizes and forwards to getFont. Returns nullptr
  // if no font is registered at that path.
  const Font* getFontByPath(std::string_view path) const;

  // Resolve a path to its FontHandle. Returns an invalid handle if unknown.
  FontHandle handleForPath(std::string_view path) const;

  size_t fontCount() const noexcept { return _fonts.size(); }
  bool hasFont(FontHandle handle) const noexcept { return _fonts.find(handle) != _fonts.end(); }

 private:
  std::unordered_map<FontHandle, std::unique_ptr<Font>> _fonts;
  std::unordered_map<std::string, FontHandle> _pathIndex;
  uint32_t _nextId = 1;

  static std::string canonicalize(std::string_view path);
};
