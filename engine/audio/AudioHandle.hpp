#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

//
// FNV-1a 32-bit hash
// https://en.wikipedia.org/wiki/Fowler–Noll–Vo_hash_function
//
inline constexpr uint32_t fnv1a(std::string_view str) {
  uint32_t hash = 2166136261u;
  for (char c : str) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 16777619u;
  }
  return hash;
}

struct AudioHandle {
  uint32_t id = 0;

  AudioHandle() = default;
  AudioHandle(std::string_view str) : id(fnv1a(str)) {}

  bool isValid() const { return id != 0; }
  bool operator==(const AudioHandle& other) const { return id == other.id; }
  bool operator!=(const AudioHandle& other) const { return id != other.id; }
};

namespace std {
template <>
struct hash<AudioHandle> {
  size_t operator()(const AudioHandle& handle) const noexcept {
    return std::hash<uint32_t>{}(handle.id);
  }
};
}  // namespace std
