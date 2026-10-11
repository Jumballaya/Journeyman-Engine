#include <gtest/gtest.h>

#include "Entities.hpp"

TEST(Entities, NewGroundStartsWithALineToDrag) {
  setComponentSchemas({{"GroundComponent", {"Ground", "Physics", "", {FieldSchema::json("chains"), FieldSchema::boolean("occludes", false)}}},
                       {"LifetimeComponent", {"Lifetime", "Physics", "", {FieldSchema::number("seconds", 1)}}}});
  const Json ground = newComponent("GroundComponent");
  EXPECT_EQ(ground["chains"], Json::parse(R"([{"points": [[-64, 0], [64, 0]]}])"));
  EXPECT_EQ(ground["occludes"], false);
  EXPECT_EQ(newComponent("LifetimeComponent"), Json::parse(R"({"seconds": 1.0})"));
}

TEST(Entities, ANewEntityLandsDownRightOfAnyItWouldCover) {
  EXPECT_EQ(freeSpot({10, 10}, {}), glm::vec2(10, 10));
  EXPECT_EQ(freeSpot({0, 0}, {{0, 0}}), glm::vec2(24, -24));  // scene Y is up: down is -y
  std::vector<glm::vec2> diagonal;
  for (int i = 0; i <= 64; ++i) diagonal.emplace_back(24.0f * i, -24.0f * i);
  EXPECT_EQ(freeSpot({0, 0}, diagonal), glm::vec2(24 * 65, -24 * 65));
}
