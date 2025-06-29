#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

struct ScriptInstanceHandle {
  uint32_t id = 0;

  bool isValid() const { return id != 0; }

  bool operator==(const ScriptInstanceHandle& other) const {
    return id == other.id;
  }
};

namespace std {
template <>
struct hash<ScriptInstanceHandle> {
  size_t operator()(const ScriptInstanceHandle& handle) const noexcept {
    return std::hash<uint32_t>{}(handle.id);
  }
};
}  // namespace std