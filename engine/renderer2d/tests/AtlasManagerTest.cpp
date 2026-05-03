#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <string>
#include <unordered_map>

#include <glm/glm.hpp>

#include "../../core/assets/AssetHandle.hpp"
#include "../AtlasManager.hpp"
#include "../TextureHandle.hpp"

namespace {

TextureHandle makeTexture(uint32_t id) {
  TextureHandle t;
  t.id = id;
  return t;
}

constexpr float kEps = 1e-6f;

}  // namespace

TEST(AtlasManager, LoadAtlasStoresRegionsAsNormalizedUVs) {
  AtlasManager mgr;
  AssetHandle h{1};
  std::unordered_map<std::string, std::array<int, 4>> regions{
      {"a", {16, 0, 32, 32}},  // half-width, top-aligned in a 64x32 atlas
  };
  mgr.loadAtlas(h, "atlas.json", makeTexture(7), 64u, 32u, regions);

  auto got = mgr.lookup(h, "a");
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->first.id, 7u);
  EXPECT_NEAR(got->second.x, 0.25f, kEps);
  EXPECT_NEAR(got->second.y, 0.0f,  kEps);
  EXPECT_NEAR(got->second.z, 0.5f,  kEps);
  EXPECT_NEAR(got->second.w, 1.0f,  kEps);
}

TEST(AtlasManager, LookupReturnsNulloptForUnknownAtlas) {
  AtlasManager mgr;
  EXPECT_FALSE(mgr.lookup(AssetHandle{42}, "anything").has_value());
}

TEST(AtlasManager, LookupReturnsNulloptForUnknownRegion) {
  AtlasManager mgr;
  AssetHandle h{1};
  std::unordered_map<std::string, std::array<int, 4>> regions{{"only", {0, 0, 8, 8}}};
  mgr.loadAtlas(h, "x.atlas.json", makeTexture(2), 8, 8, regions);

  EXPECT_TRUE(mgr.lookup(h, "only").has_value());
  EXPECT_FALSE(mgr.lookup(h, "missing").has_value());
}

TEST(AtlasManager, LookupByPathFindsHandleViaCanonicalKey) {
  AtlasManager mgr;
  AssetHandle h{3};
  std::unordered_map<std::string, std::array<int, 4>> regions{{"r", {0, 0, 16, 16}}};
  mgr.loadAtlas(h, "assets/atlases/x.atlas.json", makeTexture(9), 16, 16, regions);

  // Same path, denormalized form.
  auto got = mgr.lookupByPath("./assets/atlases/x.atlas.json", "r");
  ASSERT_TRUE(got.has_value());
  EXPECT_EQ(got->first.id, 9u);
  EXPECT_NEAR(got->second.z, 1.0f, kEps);

  // Plain forward-slash form resolves too.
  auto got2 = mgr.lookupByPath("assets/atlases/x.atlas.json", "r");
  EXPECT_TRUE(got2.has_value());

  // Wrong path → nullopt.
  EXPECT_FALSE(mgr.lookupByPath("assets/atlases/y.atlas.json", "r").has_value());
}

TEST(AtlasManager, MultipleAtlasesAreIsolated) {
  AtlasManager mgr;
  AssetHandle a{10};
  AssetHandle b{11};
  std::unordered_map<std::string, std::array<int, 4>> regionsA{{"foo", {0, 0, 32, 32}}};
  std::unordered_map<std::string, std::array<int, 4>> regionsB{{"foo", {32, 32, 16, 16}}};
  mgr.loadAtlas(a, "a.atlas.json", makeTexture(100), 64, 64, regionsA);
  mgr.loadAtlas(b, "b.atlas.json", makeTexture(200), 64, 64, regionsB);

  auto fa = mgr.lookup(a, "foo");
  auto fb = mgr.lookup(b, "foo");
  ASSERT_TRUE(fa.has_value());
  ASSERT_TRUE(fb.has_value());
  EXPECT_EQ(fa->first.id, 100u);
  EXPECT_EQ(fb->first.id, 200u);
  EXPECT_NEAR(fa->second.x, 0.0f, kEps);
  EXPECT_NEAR(fb->second.x, 0.5f, kEps);
}

TEST(AtlasManager, LoadAtlasRejectsZeroDimensions) {
  AtlasManager mgr;
  AssetHandle h{1};
  std::unordered_map<std::string, std::array<int, 4>> regions{{"r", {0, 0, 1, 1}}};
  mgr.loadAtlas(h, "bad.atlas.json", makeTexture(1), 0, 16, regions);
  EXPECT_FALSE(mgr.hasAtlas(h));
  EXPECT_EQ(mgr.atlasCount(), 0u);
}

TEST(AtlasManager, LoadAtlasRejectsInvalidTexture) {
  AtlasManager mgr;
  AssetHandle h{1};
  std::unordered_map<std::string, std::array<int, 4>> regions{{"r", {0, 0, 1, 1}}};
  TextureHandle invalid;  // id=0
  mgr.loadAtlas(h, "bad.atlas.json", invalid, 16, 16, regions);
  EXPECT_FALSE(mgr.hasAtlas(h));
}
