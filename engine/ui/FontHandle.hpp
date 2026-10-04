#pragma once

#include <cstdint>
#include <functional>

// A font in FontRegistry; 0 = none.
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
