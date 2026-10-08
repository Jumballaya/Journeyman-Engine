#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string_view>

#include <glm/glm.hpp>

#include "../../core/assets/AssetHandle.hpp"
#include "../../core/assets/AssetManager.hpp"
#include "../AtlasManager.hpp"
#include "../ShelfPacker.hpp"
#include "../TextureHandle.hpp"

namespace {

constexpr float kEps = 1e-6f;

class FakeRenderer2D {
 public:
  TextureHandle createEmptyTexture(int, int, std::string_view) {
    return TextureHandle{99};
  }
  bool subUploadTexture(TextureHandle, int, int, int, int, const void*) {
    return true;
  }
};

}  // namespace

TEST(ShelfPacker, PackShelfFitsAndReturnsSlot) {
  jm::atlas::ShelfPackerState state{64, 64, 0, 0, 0};
  auto slot = jm::atlas::packShelf(state, 32, 32);
  ASSERT_TRUE(slot.has_value());
  EXPECT_EQ(slot->x, 0u);
  EXPECT_EQ(slot->y, 0u);
  EXPECT_EQ(state.shelfTop, 0u);
  EXPECT_EQ(state.shelfHeight, 32u);
  EXPECT_EQ(state.shelfCursor, 32u);
}

TEST(ShelfPacker, PackShelfAdvancesCursorOnSameShelf) {
  jm::atlas::ShelfPackerState state{64, 64, 0, 0, 0};
  auto first = jm::atlas::packShelf(state, 32, 32);
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->x, 0u);
  EXPECT_EQ(first->y, 0u);

  auto second = jm::atlas::packShelf(state, 32, 32);
  ASSERT_TRUE(second.has_value());
  EXPECT_EQ(second->x, 32u);
  EXPECT_EQ(second->y, 0u);
  EXPECT_EQ(state.shelfCursor, 64u);
  EXPECT_EQ(state.shelfHeight, 32u);
}

TEST(ShelfPacker, PackShelfOpensNewShelfWhenWidthExceeded) {
  jm::atlas::ShelfPackerState state{64, 64, 0, 0, 0};
  ASSERT_TRUE(jm::atlas::packShelf(state, 32, 32).has_value());
  ASSERT_TRUE(jm::atlas::packShelf(state, 32, 32).has_value());

  auto third = jm::atlas::packShelf(state, 32, 32);
  ASSERT_TRUE(third.has_value());
  EXPECT_EQ(third->x, 0u);
  EXPECT_EQ(third->y, 32u);
  EXPECT_EQ(state.shelfTop, 32u);
  EXPECT_EQ(state.shelfHeight, 32u);
  EXPECT_EQ(state.shelfCursor, 32u);
}

TEST(ShelfPacker, PackShelfReturnsNulloptWhenAtlasFull) {
  jm::atlas::ShelfPackerState state{32, 32, 0, 0, 0};
  ASSERT_TRUE(jm::atlas::packShelf(state, 32, 32).has_value());
  // No width room on current shelf; new shelf would be at y=32 == atlasHeight,
  // so a 32-tall region overflows.
  EXPECT_FALSE(jm::atlas::packShelf(state, 32, 32).has_value());

  // Region wider than the atlas: rejected up front.
  jm::atlas::ShelfPackerState fresh{32, 32, 0, 0, 0};
  EXPECT_FALSE(jm::atlas::packShelf(fresh, 64, 32).has_value());
}

TEST(AtlasManagerDynamic, LookupReturnsRegionAddedDynamically) {
  AssetManager assets;
  FakeRenderer2D renderer;
  AtlasManager mgr;

  AssetHandle handle = mgr.createDynamicAtlas(assets, renderer, 64u, 64u, "nearest");
  ASSERT_TRUE(handle.isValid());
  ASSERT_TRUE(mgr.hasAtlas(handle));

  std::array<uint8_t, 32 * 32 * 4> pixels{};
  auto uv = mgr.addRegion(renderer, handle, "A", pixels.data(), 32u, 32u);
  ASSERT_TRUE(uv.has_value());
  EXPECT_NEAR(uv->x, 0.0f, kEps);
  EXPECT_NEAR(uv->y, 0.0f, kEps);
  EXPECT_NEAR(uv->z, 0.5f, kEps);
  EXPECT_NEAR(uv->w, 0.5f, kEps);

  auto got = mgr.lookup(handle, "A");
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->first.id, 99u);
  EXPECT_NEAR(got->second.x, 0.0f, kEps);
  EXPECT_NEAR(got->second.y, 0.0f, kEps);
  EXPECT_NEAR(got->second.z, 0.5f, kEps);
  EXPECT_NEAR(got->second.w, 0.5f, kEps);
}

// The glyph cache's start-over: a cleared page packs from the top again and
// forgets its regions; a removed one hands back its texture.
TEST(AtlasManagerDynamic, ClearEmptiesAPageForReuse) {
  AssetManager assets{"."};
  FakeRenderer2D renderer;
  AtlasManager atlases;
  const AssetHandle page = atlases.createDynamicAtlas(assets, renderer, 64, 64, "linear");
  const std::array<uint8_t, 32 * 32 * 4> pixels{};
  const auto first = atlases.addRegion(renderer, page, "a", pixels.data(), 32, 32);
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(atlases.addRegion(renderer, page, "b", pixels.data(), 32, 32).has_value());

  EXPECT_TRUE(atlases.clearDynamicAtlas(page));
  EXPECT_FALSE(atlases.lookup(page, "a").has_value());
  const auto again = atlases.addRegion(renderer, page, "c", pixels.data(), 32, 32);
  ASSERT_TRUE(again.has_value());
  EXPECT_NEAR(again->x, first->x, kEps);  // back at the top-left
  EXPECT_NEAR(again->y, first->y, kEps);
}

TEST(AtlasManagerDynamic, RemoveForgetsTheAtlasAndReturnsItsTexture) {
  AssetManager assets{"."};
  FakeRenderer2D renderer;
  AtlasManager atlases;
  const AssetHandle page = atlases.createDynamicAtlas(assets, renderer, 64, 64, "linear");
  EXPECT_EQ(atlases.removeAtlas(page), TextureHandle{99});
  EXPECT_FALSE(atlases.hasAtlas(page));
  EXPECT_FALSE(atlases.clearDynamicAtlas(page));
  EXPECT_FALSE(atlases.removeAtlas(page).isValid());
}
