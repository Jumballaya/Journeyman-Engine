#include <gtest/gtest.h>

#include "TileGrid.hpp"

namespace {

// Every image resolves to a texture whose id encodes the name's length, so
// tests can tell which name was picked without a renderer.
std::shared_ptr<const Tileset> tileset(const char* json, std::vector<std::string>* names = nullptr) {
  return std::make_shared<const Tileset>(Tileset::parse(
      nlohmann::json::parse(json), nlohmann::json{{"theme", "over_"}},
      [names](const std::string& ref) -> std::optional<TileImage> {
        if (names) names->push_back(ref);
        if (ref.find("missing") != std::string::npos) return std::nullopt;
        return TileImage{TextureHandle{static_cast<uint32_t>(ref.size())}, {}, {16, 16}};
      }));
}

const char* kTiles = R"({"atlas": "a.json", "tiles": {
  "#": {"solid": true, "image": "{theme}ground", "edges": [{"open": "N", "image": "{theme}ground_top"}]},
  ",": {"image": "path_{mask}", "joins": ",E"},
  "~": {"image": "lava_{frame}", "frames": 2, "frameDuration": 0.5, "tags": ["deadly"]},
  "E": {"solid": true, "under": ",."},
  ".": {"image": "grass"},
  "*": {"image": ["spark_1", "spark_2"], "frameDuration": 0.5}}})";

}  // namespace

TEST(TileGrid, RowsReadTopFirstAndOutsideIsConfigurable) {
  TileGrid grid({"ab", "cd"}, tileset(kTiles), 16, {'#', '#', '.', '.'});
  EXPECT_EQ(grid.width(), 2);
  EXPECT_EQ(grid.at(0, 0), 'c');  // bottom-left
  EXPECT_EQ(grid.at(1, 1), 'b');
  EXPECT_EQ(grid.at(-1, 0), '#');
  EXPECT_EQ(grid.at(0, 5), '.');
  EXPECT_TRUE(grid.solid(-1, 0));
  EXPECT_FALSE(grid.solid(0, -1));
  grid.set(0, 0, '#');
  EXPECT_TRUE(grid.solid(0, 0));
  grid.set(9, 9, '#');  // outside: ignored
}

TEST(TileGrid, TagsAndAnimatedFrames) {
  std::vector<std::string> names;
  TileGrid grid({"~"}, tileset(kTiles, &names), 16, {});
  EXPECT_TRUE(grid.is(0, 0, "deadly"));
  EXPECT_FALSE(grid.is(0, 0, "solid"));
  const TileDef* lava = grid.def(0, 0);
  EXPECT_EQ(lava->image(0, 0.2f)->texture, lava->images[0][0].texture);
  EXPECT_EQ(lava->image(0, 0.7f)->texture, lava->images[0][1].texture);
  EXPECT_NE(std::find(names.begin(), names.end(), "a.json#lava_1"), names.end());
  // Frames can also be listed by name.
  TileGrid listed({"*"}, tileset(kTiles), 16, {});
  const TileDef* spark = listed.def(0, 0);
  ASSERT_EQ(spark->images[0].size(), 2u);
  EXPECT_EQ(spark->image(0, 0.7f)->texture.id, std::string("a.json#spark_2").size());
}

TEST(TileGrid, EdgeRulesAndMaskTemplates) {
  std::vector<std::string> names;
  TileGrid grid({"..", "##", "##"}, tileset(kTiles, &names), 16, {});
  const TileDef* ground = grid.def(0, 0);
  // The top row of ground is open to the north; the row below isn't.
  EXPECT_EQ(grid.mask(0, 1, '#', *ground) & edges::N, edges::N);
  EXPECT_EQ(grid.mask(0, 0, '#', *ground) & edges::N, 0);
  EXPECT_NE(std::find(names.begin(), names.end(), "a.json#over_ground_top"), names.end());
  EXPECT_EQ(ground->image(edges::N, 0)->texture.id, std::string("a.json#over_ground_top").size());
  EXPECT_EQ(ground->image(edges::S, 0)->texture.id, std::string("a.json#over_ground").size());
}

TEST(TileGrid, UnderPicksANeighbourAndJoinsItsTerrain) {
  TileGrid grid({",E.", "..."}, tileset(kTiles), 16, {});
  EXPECT_EQ(grid.under(1, 1), ',');  // a road beside it
  EXPECT_EQ(grid.under(0, 0), 0);    // grass has nothing beneath
  TileGrid alone({".E."}, tileset(kTiles), 16, {});
  EXPECT_EQ(alone.under(1, 0), '.');  // the fallback
  // The road at (0, 1) joins the person standing on road to its east.
  const TileDef* road = grid.def(0, 1);
  EXPECT_EQ(grid.mask(0, 1, ',', *road) & edges::E, 0);
  EXPECT_EQ(grid.mask(0, 1, ',', *road) & edges::S, edges::S);
}

TEST(TileGrid, BoxesStopFlushAndReportTheTile) {
  // A floor with a wall at x = 3.
  TileGrid grid({"...#", "...#", "####"}, tileset(kTiles), 16, {'#', '#', '.', '.'});
  // Falling onto the floor (top at y = 16).
  auto fall = grid.move({24, 30}, {4, 6}, {0, -20});
  EXPECT_NEAR(fall.position.y, 22.01f, 0.001f);
  EXPECT_EQ(fall.hit.y, -1);
  EXPECT_EQ(fall.hitTile, glm::ivec2(1, 0));
  // Walking into the wall (left face at x = 48).
  auto walk = grid.move({40, 24}, {4, 6}, {10, 0});
  EXPECT_NEAR(walk.position.x, 43.99f, 0.001f);
  EXPECT_EQ(walk.hit.x, 1);
  EXPECT_EQ(walk.hitTile.x, 3);
  // A fast move can't tunnel through the floor.
  auto fast = grid.move({24, 30}, {4, 6}, {0, -200});
  EXPECT_EQ(fast.hit.y, -1);
  EXPECT_GT(fast.position.y, 16.0f);
}

TEST(TileGrid, BlockedMovesSlideTowardOpenings) {
  // A one-tile doorway at x = 1 in a wall row.
  TileGrid grid({"...", "#.#", "..."}, tileset(kTiles), 16, {});
  // Moving up, 3 units right of the doorway's center: nudged left.
  auto up = grid.move({27, 8}, {6, 6}, {0, 4}, 6);
  EXPECT_EQ(up.hit.y, 1);
  EXPECT_LT(up.position.x, 27.0f);
  auto stuck = grid.move({27, 8}, {6, 6}, {0, 4}, 0);
  EXPECT_FLOAT_EQ(stuck.position.x, 27.0f);
}
