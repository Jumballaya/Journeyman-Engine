#include <gtest/gtest.h>

#include "../../physics2d/Queries.hpp"
#include "TileMapTerrainSystem.hpp"

namespace {

using nlohmann::json;

// A 4x2-tile (64x32 px) map with one ground line `y` px up from its bottom.
TileGrid groundAt(float y) {
  const json map = {{"width", 4}, {"height", 2}, {"tilewidth", 16}, {"tileheight", 16},
                    {"layers", json::array({{{"type", "objectgroup"}, {"objects", json::array({
                        {{"id", 1}, {"type", "ground"}, {"x", 0}, {"y", 32 - y},
                         {"polyline", json::array({{{"x", 0}, {"y", 0}}, {{"x", 64}, {"y", 0}}})}}})}}})}};
  return TileGrid::parse(map, "level.tmj", [](const std::string&) { return nullptr; },
                         [](const std::string&, std::optional<glm::ivec4>) { return std::nullopt; });
}

}  // namespace

TEST(TileMapTerrain, AMapsGroundIsItsEntitysTerrainAndFollowsAReload) {
  World world;
  world.registerComponent<TransformComponent>();
  world.registerComponent<TileMapComponent>();
  world.registerComponent<GroundComponent>();
  TileMapTerrainSystem sync;
  const EntityId map = world.createEntity();
  world.addComponent<TransformComponent>(map).position = {100, 0, 0};
  world.addComponent<TileMapComponent>(map).grid = groundAt(10);
  EXPECT_FALSE(raycast(world, {120, 50}, {0, -1}, 100, kTerrainLayers));  // not until the system runs

  sync.update(world, 0);
  auto hit = raycast(world, {120, 50}, {0, -1}, 100, kTerrainLayers);
  ASSERT_TRUE(hit);
  EXPECT_EQ(hit->entity, map);
  EXPECT_FLOAT_EQ(hit->point.y, 10);

  world.getComponent<TileMapComponent>(map)->grid = groundAt(20);  // what loading another map does
  sync.update(world, 0);
  hit = raycast(world, {120, 50}, {0, -1}, 100, kTerrainLayers);
  ASSERT_TRUE(hit);
  EXPECT_FLOAT_EQ(hit->point.y, 20);

  world.removeComponent<TileMapComponent>(map);  // the map goes, and its ground with it
  sync.update(world, 0);
  EXPECT_FALSE(raycast(world, {120, 50}, {0, -1}, 100, kTerrainLayers));
  EXPECT_FALSE(world.hasComponent<GroundComponent>(map));
}
