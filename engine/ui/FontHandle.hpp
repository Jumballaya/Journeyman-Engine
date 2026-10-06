#pragma once

#include <cstdint>
#include <functional>

// A font in FontRegistry; 0 = none.
struct FontHandle {
  uint32_t id = 0;
  constexpr bool isValid() const noexcept { return id != 0; }
  constexpr bool operator==(const FontHandle&) const noexcept = default;
};

template <>
struct std::hash<FontHandle> {
  size_t operator()(const FontHandle& h) const noexcept { return std::hash<uint32_t>{}(h.id); }
};
