#include <gtest/gtest.h>

#include "Letterbox.hpp"

// A 480x640 (portrait) game in a 1920x1080 window: bars on the sides.
TEST(Letterbox, WiderWindowGetsSideBars) {
  const glm::vec4 vp = letterbox::fit(1920, 1080, 480, 640);
  EXPECT_EQ(vp, glm::vec4(555, 0, 810, 1080));  // 1080/640 = 1.6875; 480 * 1.6875 = 810
}

TEST(Letterbox, TallerWindowGetsBarsAboveAndBelow) {
  const glm::vec4 vp = letterbox::fit(1280, 1280, 320, 240);
  EXPECT_EQ(vp, glm::vec4(0, 160, 1280, 960));
}

TEST(Letterbox, AnExactFitHasNoBars) {
  EXPECT_EQ(letterbox::fit(1280, 720, 640, 360), glm::vec4(0, 0, 1280, 720));
}

// Odd sizes: whole pixels, and centered by rounding down.
TEST(Letterbox, OddSizesStayWholePixels) {
  const glm::vec4 vp = letterbox::fit(1001, 751, 320, 240);
  EXPECT_EQ(vp, glm::vec4(0, 0, 1001, 750));  // 1001/320 = 3.128..; 240 * that = 750.7 -> 750
  EXPECT_EQ(vp.x, std::floor(vp.x));
  EXPECT_EQ(vp.y, std::floor(vp.y));
}

// The mouse: window px from the top-left, the game's px from its top-left.
TEST(Letterbox, PointerMapsIntoTheGame) {
  const glm::vec4 vp = letterbox::fit(1920, 1080, 480, 640);  // x 555..1365, scale 1.6875
  EXPECT_EQ(letterbox::toLogical({555, 0}, vp, 1080, 480), glm::vec2(0, 0));
  const glm::vec2 centre = letterbox::toLogical({960, 540}, vp, 1080, 480);
  EXPECT_NEAR(centre.x, 240.0f, 1e-3f);
  EXPECT_NEAR(centre.y, 320.0f, 1e-3f);
  EXPECT_LT(letterbox::toLogical({100, 540}, vp, 1080, 480).x, 0.0f);  // over the left bar
}

TEST(Letterbox, PointerAccountsForBarsAbove) {
  const glm::vec4 vp = letterbox::fit(1280, 1280, 320, 240);  // y 160..1120 from the bottom: 160 px bar on top
  EXPECT_EQ(letterbox::toLogical({0, 160}, vp, 1280, 320), glm::vec2(0, 0));
  EXPECT_LT(letterbox::toLogical({0, 100}, vp, 1280, 320).y, 0.0f);  // over the top bar
}
