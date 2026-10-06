#include "FontRegistry.hpp"

namespace {

std::string canonicalize(const std::filesystem::path& path) { return path.lexically_normal().generic_string(); }

}  // namespace

FontHandle FontRegistry::registerFont(const std::filesystem::path& sourcePath, std::unique_ptr<Font> font) {
  auto [it, added] = _pathIndex.try_emplace(canonicalize(sourcePath), FontHandle{_nextId});
  if (added) ++_nextId;
  _fonts[it->second] = std::move(font);
  return it->second;
}

const Font* FontRegistry::getFont(FontHandle handle) const noexcept {
  auto it = _fonts.find(handle);
  return it != _fonts.end() ? it->second.get() : nullptr;
}

FontHandle FontRegistry::handleForPath(std::string_view path) const {
  auto it = _pathIndex.find(canonicalize(path));
  return it != _pathIndex.end() ? it->second : FontHandle{};
}
