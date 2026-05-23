#pragma once

#include <cstdint>
#include <functional>

// FontHandle is a pure asset-lifetime handle minted by FontRegistry. It mirrors
// AssetHandle's shape exactly (NOT TextureHandle, which carries a 2D/cubemap
// Type enum FontHandle has no use for). id == 0 is the invalid sentinel.
struct FontHandle {
  uint32_t id = 0;
  constexpr bool isValid() const noexcept { return id != 0; }
  constexpr bool operator==(const FontHandle& other) const noexcept { return id == other.id; }
  constexpr bool operator!=(const FontHandle& other) const noexcept { return id != other.id; }
};

namespace std {
template <>
struct hash<FontHandle> {
  size_t operator()(const FontHandle& h) const noexcept {
    return std::hash<uint32_t>{}(h.id);
  }
};
}  // namespace std
