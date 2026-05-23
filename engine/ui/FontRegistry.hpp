#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

#include "../core/assets/AssetHandle.hpp"
#include "Font.hpp"
#include "FontHandle.hpp"

// FontRegistry: path-keyed + handle-keyed registry of loaded fonts. Resembles
// AtlasManager's dual-index shape (_fonts handle->value, _pathIndex
// canonical-path->handle, lexically_normal canonicalization), but DIVERGES on
// value storage: AtlasManager stores AtlasInfo INLINE because it's move-safe;
// FontRegistry holds each Font via unique_ptr because Font owns a heap
// stbtt_fontinfo* that indexes into a retained byte buffer — both must keep a
// stable address across _fonts rehashes. All mutations are main-thread only.
class FontRegistry {
 public:
  // Register a parsed font. Mints a FontHandle, stores the font in _fonts, and
  // indexes the canonical path. Re-registering the same path reuses the
  // existing FontHandle and overwrites the stored Font (hot-reload). Stale
  // handles cached elsewhere remain valid (same id) and resolve to the new
  // Font; no eviction or notification is shipped in G.1. The assetHandle
  // parameter is currently unused — kept in the signature so a later phase can
  // add an _assetToFont index without an API break.
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
