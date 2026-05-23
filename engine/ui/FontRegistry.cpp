#include "FontRegistry.hpp"

std::string FontRegistry::canonicalize(std::string_view path) {
  return std::filesystem::path(path).lexically_normal().generic_string();
}

FontHandle FontRegistry::registerFont(AssetHandle /*assetHandle*/,
                                      const std::filesystem::path& sourcePath,
                                      std::unique_ptr<Font> font) {
  const std::string key = canonicalize(sourcePath.string());

  auto it = _pathIndex.find(key);
  if (it != _pathIndex.end()) {
    // Hot-reload: reuse the existing handle, overwrite the stored Font. The
    // replaced Font is destroyed at assignment.
    _fonts[it->second] = std::move(font);
    return it->second;
  }

  const FontHandle handle{_nextId++};
  _fonts.emplace(handle, std::move(font));
  _pathIndex.emplace(key, handle);
  return handle;
}

const Font* FontRegistry::getFont(FontHandle handle) const noexcept {
  auto it = _fonts.find(handle);
  return (it != _fonts.end()) ? it->second.get() : nullptr;
}

const Font* FontRegistry::getFontByPath(std::string_view path) const {
  return getFont(handleForPath(path));
}

FontHandle FontRegistry::handleForPath(std::string_view path) const {
  auto it = _pathIndex.find(canonicalize(path));
  return (it != _pathIndex.end()) ? it->second : FontHandle{};
}
