#include <gtest/gtest.h>

#include "Project.hpp"

TEST(Project, ANormalMapIsItsImagesNotAPictureOfItsOwn) {
  EXPECT_EQ(normalMapPath("assets/hero.png"), "assets/hero.normal.png");
  EXPECT_EQ(normalMapPath("assets/hero.normal.png"), "");
  EXPECT_EQ(normalMapPath("assets/hero.jpg"), "");
  EXPECT_TRUE(assetMatches("assets/hero.png", {".png"}));
  EXPECT_FALSE(assetMatches("assets/hero.normal.png", {".png"}));
  EXPECT_TRUE(assetMatches("assets/hero.normal.png", {".normal.png"}));
  EXPECT_TRUE(assetMatches("assets/hero.normal.png", {}));  // any file
}

TEST(Project, AnAtlasSourcesRegionIsWhatJmBuildNamesIt) {
  EXPECT_EQ(atlasRegion("art/hero.png"), "hero");
  EXPECT_EQ(atlasRegion("art/hero.json"), "hero_0");  // an Aseprite sheet: its first frame
  EXPECT_EQ(atlasRegion("art/hero.JSON"), "hero_0");
}
