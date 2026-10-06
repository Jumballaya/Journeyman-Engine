#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

// Names a GPU shader program owned by GpuResources; 0 = none.
struct ShaderHandle {
  uint32_t id = 0;

  bool isValid() const { return id != 0; }
  bool operator==(const ShaderHandle&) const = default;
};

template <>
struct std::hash<ShaderHandle> {
  size_t operator()(const ShaderHandle& handle) const { return std::hash<uint32_t>()(handle.id); }
};
