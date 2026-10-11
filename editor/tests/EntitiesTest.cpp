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
