#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

struct AssetHandle {
  uint32_t id = 0;

  constexpr bool operator==(const AssetHandle& other) const noexcept { return id == other.id; }
  constexpr bool operator!=(const AssetHandle& other) const noexcept { return id != other.id; }

  constexpr bool isValid() const noexcept { return id != 0; }
};

namespace std {
template <>
struct hash<AssetHandle> {
  size_t operator()(const AssetHandle& handle) const noexcept {
    return std::hash<uint32_t>{}(handle.id);
  }
};
};  // namespace std