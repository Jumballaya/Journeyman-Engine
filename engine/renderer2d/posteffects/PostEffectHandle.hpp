#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

struct PostEffectHandle {
  uint32_t id = 0;

  bool operator==(const PostEffectHandle& other) const = default;

  bool isValid() const {
    return id != 0;
  }
};

namespace std {
template <>
struct hash<PostEffectHandle> {
  inline size_t operator()(const PostEffectHandle& handle) const {
    return hash<uint32_t>()(handle.id);
  }
};
}  // namespace std
