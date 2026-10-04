#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

// Names a GPU texture owned by GpuResources; 0 = none.
struct TextureHandle {
  uint32_t id = 0;

  bool isValid() const { return id != 0; }
  bool operator==(const TextureHandle&) const = default;
};

template <>
struct std::hash<TextureHandle> {
  size_t operator()(const TextureHandle& handle) const { return std::hash<uint32_t>()(handle.id); }
};
