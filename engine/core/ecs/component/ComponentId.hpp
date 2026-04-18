#pragma once

#include <cstdint>
#include <string_view>

using ComponentId = uint32_t;

// FNV-1a 32-bit hash. Deterministic across runs, builds, and registration
// order — required for stable serialization of ECS data.
constexpr ComponentId HashComponentName(std::string_view s) {
  uint32_t h = 0x811c9dc5u;
  for (char c : s) {
    h ^= static_cast<uint8_t>(c);
    h *= 0x01000193u;
  }
  return h;
}

template <typename T>
ComponentId GetComponentId() {
  static const ComponentId id = HashComponentName(T::typeName);
  return id;
}