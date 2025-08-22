#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "gl/Texture2D.hpp"

struct ShaderHandle {
  ShaderHandle() = default;
  ~ShaderHandle() = default;

  inline bool operator==(const ShaderHandle& other) const {
    return other.id == id;
  }

  bool isValid() const {
    return id != 0;
  }

  uint32_t id = 0;
};

namespace std {
template <>
struct hash<ShaderHandle> {
  inline size_t operator()(const ShaderHandle& handle) const {
    return hash<uint32_t>()(handle.id);
  }
};
}  // namespace std