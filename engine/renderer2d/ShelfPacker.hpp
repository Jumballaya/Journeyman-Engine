#pragma once

#include <cstdint>
#include <optional>

namespace jm::atlas {

struct ShelfPackerState {
  uint32_t atlasWidth = 0;
  uint32_t atlasHeight = 0;
  uint32_t shelfTop = 0;
  uint32_t shelfHeight = 0;
  uint32_t shelfCursor = 0;
};

struct PackedSlot {
  uint32_t x;
  uint32_t y;
};

// Returns nullopt if the (w, h) doesn't fit. On success, mutates state to
// reflect the new shelf cursor / top / height and returns the assigned (x, y).
std::optional<PackedSlot> packShelf(ShelfPackerState& state, uint32_t w, uint32_t h);

}  // namespace jm::atlas
