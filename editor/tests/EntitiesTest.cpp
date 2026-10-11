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

// An override holds only what was edited: untouched keys of an inherited group keep following the prefab.
TEST(Entities, AnInheritedGroupEditOverridesOnlyThatField) {
  const Json prefab = Json::parse(R"({"bounds": {"min": [-150, -70], "max": [150, 70]}, "regions": [{"min": [0, 0], "max": [10, 10]}]})");
  Json override = Json::object();
  setField(override, {"bounds", "min"}, Json::array({-140, -70}), prefab);
  EXPECT_EQ(override, Json::parse(R"({"bounds": {"min": [-140, -70]}})"));
  Json changed = prefab;
  changed["bounds"]["max"] = Json::array({190, 90});
  EXPECT_EQ(mergeDeep(changed, override)["bounds"]["max"], Json::parse("[190, 90]"));

  setField(override, {"regions", "0", "max"}, Json::array({20, 20}), prefab);  // a list: whole, as lists replace
  EXPECT_EQ(override["regions"], Json::parse(R"([{"min": [0, 0], "max": [20, 20]}])"));
}

TEST(Entities, ANewEntityLandsDownRightOfAnyItWouldCover) {
  EXPECT_EQ(freeSpot({10, 10}, {}), glm::vec2(10, 10));
  EXPECT_EQ(freeSpot({0, 0}, {{0, 0}}), glm::vec2(24, -24));  // scene Y is up: down is -y
  std::vector<glm::vec2> diagonal;
  for (int i = 0; i <= 64; ++i) diagonal.emplace_back(24.0f * i, -24.0f * i);
  EXPECT_EQ(freeSpot({0, 0}, diagonal), glm::vec2(24 * 65, -24 * 65));
}
