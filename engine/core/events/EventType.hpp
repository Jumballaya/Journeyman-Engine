#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

using EventType = uint32_t;

//
// FNV-1a 32-bit hash
// https://en.wikipedia.org/wiki/Fowler–Noll–Vo_hash_function
//
inline constexpr EventType createEventType(std::string_view str) {
  uint32_t hash = 2166136261u;
  for (char c : str) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 16777619u;
  }
  return hash;
}
