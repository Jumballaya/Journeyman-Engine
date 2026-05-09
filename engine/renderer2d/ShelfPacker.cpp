#include "ShelfPacker.hpp"

namespace jm::atlas {

std::optional<PackedSlot> packShelf(ShelfPackerState& state, uint32_t w, uint32_t h) {
  if (w == 0 || h == 0 || w > state.atlasWidth || h > state.atlasHeight) {
    return std::nullopt;
  }

  // Try to fit on current shelf.
  if (state.shelfCursor + w <= state.atlasWidth && h <= state.shelfHeight) {
    PackedSlot slot{state.shelfCursor, state.shelfTop};
    state.shelfCursor += w;
    return slot;
  }

  // Open a new shelf below.
  const uint32_t newTop = state.shelfTop + state.shelfHeight;
  if (newTop + h > state.atlasHeight) {
    return std::nullopt;
  }
  state.shelfTop = newTop;
  state.shelfHeight = h;
  state.shelfCursor = w;
  return PackedSlot{0, newTop};
}

}  // namespace jm::atlas
