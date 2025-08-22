#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>

#include "gl/Texture2D.hpp"

struct TextureHandle {
  enum Type {
    _2D,
    _2DArray
  };

  TextureHandle() = default;
  ~TextureHandle() = default;

  inline bool operator==(const TextureHandle& other) const {
    return other.id == id;
  }

  bool isValid() const {
    return id != 0;
  }

  uint32_t id = 0;
  Type type = Type::_2D;
};

namespace std {
template <>
struct hash<TextureHandle> {
  inline size_t operator()(const TextureHandle& handle) const {
    return hash<uint32_t>()(handle.id);
  }
};
}  // namespace std