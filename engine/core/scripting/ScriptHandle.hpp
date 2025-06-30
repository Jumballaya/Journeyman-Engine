#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

struct ScriptHandle {
  uint32_t id = 0;
  bool isValid() const { return id != 0; }
  bool operator==(const ScriptHandle& other) const { return id == other.id; }
};

namespace std {
template <>
struct hash<ScriptHandle> {
  size_t operator()(const ScriptHandle& handle) const noexcept {
    return std::hash<uint32_t>{}(handle.id);
  }
};
}  // namespace std