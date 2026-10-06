#pragma once

#include <compare>
#include <cstdint>
#include <functional>

// An index plus the generation it was handed out in, so stale ids of a reused
// index are detectably dead.
struct EntityId {
  uint32_t index;
  uint32_t generation;

  auto operator<=>(const EntityId &) const = default;
};

template <> struct std::hash<EntityId> {
  size_t operator()(const EntityId &id) const noexcept {
    return std::hash<uint64_t>{}(static_cast<uint64_t>(id.generation) << 32 | id.index);
  }
};
