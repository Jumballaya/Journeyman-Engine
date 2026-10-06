#include <gtest/gtest.h>

#include "TiledFiles.hpp"

namespace {

// Edge set "path": tile `mask` has a side joined where its bit (N 1, E 2, S 4, W 8) is clear.
Json pathTileset() {
  Json wangtiles = Json::array();
  for (int m = 0; m < 16; ++m) {
    const int n = m & 1 ? 0 : 1, e = m & 2 ? 0 : 1, s = m & 4 ? 0 : 1, w = m & 8 ? 0 : 1;
    wangtiles.push_back({{"tileid", m}, {"wangid", {n, 0, e, 0, s, 0, w, 0}}});
  }
  return {{"tilecount", 16}, {"wangsets", {{{"name", "path"}, {"type", "edge"}, {"wangtiles", wangtiles}}}}};
}

// Corner set "grass": tile id bits are its grass corners (top-right 1, bottom-right 2, bottom-left 4, top-left 8).
Json grassTileset() {
  Json wangtiles = Json::array();
  for (int b = 1; b < 16; ++b) {
    wangtiles.push_back({{"tileid", b}, {"wangid", {0, b & 1 ? 1 : 0, 0, b & 2 ? 1 : 0, 0, b & 4 ? 1 : 0, 0, b & 8 ? 1 : 0}}});
  }
  return {{"tilecount", 16}, {"wangsets", {{{"name", "grass"}, {"type", "corner"}, {"wangtiles", wangtiles}}}}};
}

uint32_t local(const Json& map, glm::ivec2 cell) { return tiled::gidAt(map, 0, cell) ? tiled::gidAt(map, 0, cell) - 1 : 99; }

}  // namespace

TEST(TiledFiles, EdgeTerrainJoinsPaintedCells) {
  Json map = tiled::newMap({5, 5}, {16, 16});
  const Json set = pathTileset();
  for (int x = 1; x <= 3; ++x) EXPECT_TRUE(tiled::paintTerrain(map, 0, {x, 2}, set, 1, 0, 1));
  EXPECT_EQ(local(map, {1, 2}), 1u | 4 | 8);  // open north, south, west
  EXPECT_EQ(local(map, {2, 2}), 1u | 4);      // joined both ways
  EXPECT_EQ(local(map, {3, 2}), 1u | 2 | 4);
  EXPECT_EQ(tiled::gidAt(map, 0, {2, 3}), 0u);  // neighbours aren't painted
  // Erasing the middle splits the path into two ends.
  EXPECT_TRUE(tiled::paintTerrain(map, 0, {2, 2}, set, 1, 0, 0));
  EXPECT_EQ(tiled::gidAt(map, 0, {2, 2}), 0u);
  EXPECT_EQ(local(map, {1, 2}), 15u);
  EXPECT_FALSE(tiled::paintTerrain(map, 0, {1, 2}, set, 1, 0, 1));  // already painted
}

TEST(TiledFiles, CornerTerrainPaintsSharedCorners) {
  Json map = tiled::newMap({5, 5}, {16, 16});
  tiled::paintTerrain(map, 0, {2, 2}, grassTileset(), 1, 0, 1);
  EXPECT_EQ(local(map, {2, 2}), 15u);
  EXPECT_EQ(local(map, {3, 2}), 4u | 8);  // its left corners
  EXPECT_EQ(local(map, {3, 3}), 4u);      // its bottom-left corner
  EXPECT_EQ(tiled::gidAt(map, 0, {4, 2}), 0u);
}

TEST(TiledFiles, MovesRewriteRelativePaths) {
  Json map = tiled::newMap({1, 1}, {16, 16});
  map["tilesets"].push_back({{"firstgid", 1}, {"source", "../tiles/t.tsj"}});
  map["layers"].push_back({{"type", "imagelayer"}, {"image", "sky.png"}});
  EXPECT_EQ(tiled::references(map, "assets/maps/a.tmj"), (std::vector<std::string>{"assets/maps/sky.png", "assets/tiles/t.tsj"}));
  auto moved = [](const std::string& p) { return p == "assets/tiles/t.tsj" ? std::string("assets/ts/t.tsj") : p; };
  EXPECT_TRUE(tiled::rebase(map, "assets/maps/a.tmj", "assets/levels/deep/a.tmj", moved));
  EXPECT_EQ(map["tilesets"][0]["source"], "../../ts/t.tsj");
  EXPECT_EQ(map["layers"][1]["image"], "../../maps/sky.png");
}

TEST(TiledFiles, ResizeKeepsTheBottomLeftAndObjects) {
  Json map = tiled::newMap({2, 2}, {16, 16});
  tiled::setGid(map, 0, {0, 0}, 7);
  const int objects = tiled::addLayer(map, "objectgroup", "things");
  const int id = tiled::addObject(map, objects, {4, 6, 8, 8}, "spawn");
  EXPECT_EQ(tiled::objectRect(map, *tiled::findObject(map, id)), glm::vec4(4, 6, 8, 8));
  EXPECT_EQ(tiled::objectAt(map, {8, 10}), id);
  tiled::resize(map, {3, 4});
  EXPECT_EQ(tiled::gidAt(map, 0, {0, 0}), 7u);
  EXPECT_EQ(map["layers"][0]["data"].size(), 12u);
  EXPECT_EQ(tiled::objectRect(map, *tiled::findObject(map, id)), glm::vec4(4, 6, 8, 8));
  // Written with one row per line, and read back the same.
  EXPECT_EQ(Json::parse(tiled::serializeMap(map)), map);
}

TEST(TiledFiles, TilesetsTakeTheGidsAfterTheOthers) {
  Json map = tiled::newMap({1, 1}, {16, 16});
  auto span = [](const std::string& p) { return p == "assets/a.tsj" ? 10u : 3u; };
  EXPECT_EQ(tiled::addTileset(map, "assets/m.tmj", "assets/a.tsj", span), 1u);
  EXPECT_EQ(tiled::addTileset(map, "assets/m.tmj", "assets/b.tsj", span), 11u);
  EXPECT_EQ(tiled::addTileset(map, "assets/m.tmj", "assets/a.tsj", span), 1u);  // already there
  tiled::setGid(map, 0, {0, 0}, 12);
  tiled::removeTileset(map, "assets/m.tmj", "assets/b.tsj");
  EXPECT_EQ(tiled::gidAt(map, 0, {0, 0}), 0u);
  EXPECT_EQ(map["tilesets"].size(), 1u);
}
