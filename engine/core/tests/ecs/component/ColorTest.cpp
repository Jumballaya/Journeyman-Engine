#include <gtest/gtest.h>

#include "component/Color.hpp"

// A color is what jm's schema says one is: 3 or 4 numbers, or #hex.
TEST(Color, ArraysAndHexAreColors) {
  EXPECT_EQ(colorOf(nlohmann::json::parse("[1, 0, 0.5, 0.25]")), glm::vec4(1, 0, 0.5f, 0.25f));
  EXPECT_EQ(colorOf(nlohmann::json::parse("[0, 1, 0]")), glm::vec4(0, 1, 0, 1));
  EXPECT_EQ(colorOf("#ff0000"), glm::vec4(1, 0, 0, 1));
  EXPECT_EQ(colorOf("#F00"), glm::vec4(1, 0, 0, 1));
  EXPECT_EQ(colorOf("#00ff0080"), glm::vec4(0, 1, 0, 128 / 255.0f));
  EXPECT_EQ(colorOf("#0f08"), glm::vec4(0, 1, 0, 136 / 255.0f));
}

TEST(Color, AnythingElseIsNone) {
  for (const char* json : {R"("red")", R"("#ff000")", R"("#gg0000")", "[1, 0]", "[1, 0, 0, 1, 1]", R"([1, "0", 0])", "{}", "1", "null"})
    EXPECT_FALSE(colorOf(nlohmann::json::parse(json))) << json;
}
