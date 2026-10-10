#include <gtest/gtest.h>

#include "TileGrid.hpp"

namespace {

using nlohmann::json;

// Images resolve to a texture whose id is the path's length, with the rect as texRect (pixels).
std::optional<TileImage> fakeImage(const std::string& path, std::optional<glm::ivec4> rect) {
  if (path.find("missing") != std::string::npos) return std::nullopt;
  const glm::vec4 r = rect ? glm::vec4(*rect) : glm::vec4(0, 0, 64, 64);
  return TileImage{TextureHandle{static_cast<uint32_t>(path.size())}, r, glm::vec2(r.z, r.w)};
}

// tiles.tsj: a 4x1 sheet of 16px tiles: 0 "ground" (solid), 1 "grass", 2 "lava" (deadly, animated 0.5s
// between 2 and 3), 3 "bridge" (solid false).
const json kTileset = json::parse(R"({"type": "tileset", "tilewidth": 16, "tileheight": 16, "tilecount": 4,
  "columns": 4, "image": "../img/tiles.png", "imagewidth": 64, "imageheight": 16,
  "tiles": [{"id": 0, "type": "ground", "properties": [{"name": "solid", "type": "bool", "value": true}]},
            {"id": 1, "type": "grass"},
            {"id": 2, "type": "lava", "properties": [{"name": "deadly", "type": "bool", "value": true}],
             "animation": [{"tileid": 2, "duration": 500}, {"tileid": 3, "duration": 500}]},
            {"id": 3, "type": "bridge", "properties": [{"name": "solid", "type": "bool", "value": false}]}]})");

// Rows top first: '#' ground, '.' grass, '~' lava, '=' bridge, ' ' empty.
json layer(const std::string& name, const std::vector<std::string>& rows) {
  json data = json::array();
  for (const auto& row : rows) {
    for (char c : row) data.push_back(c == '#' ? 1 : c == '.' ? 2 : c == '~' ? 3 : c == '=' ? 4 : 0);
  }
  return {{"type", "tilelayer"}, {"name", name}, {"width", rows[0].size()}, {"height", rows.size()}, {"data", data}};
}

TileGrid mapOf(json layers, json extra = json::object()) {
  const int width = static_cast<int>(layers[0]["width"]), height = static_cast<int>(layers[0]["height"]);
  json map = {{"type", "map"}, {"orientation", "orthogonal"}, {"width", width}, {"height", height},
              {"tilewidth", 16}, {"tileheight", 16}, {"layers", layers},
              {"tilesets", json::array({{{"firstgid", 1}, {"source", "../tiles/tiles.tsj"}}})}};
  map.update(extra);
  auto load = [](const std::string& path) -> std::shared_ptr<const Tileset> {
    EXPECT_EQ(path, "assets/tiles/tiles.tsj");  // relative to the map
    return std::make_shared<const Tileset>(Tileset::parse(kTileset, path, fakeImage));
  };
  return TileGrid::parse(map, "assets/maps/level.tmj", load, fakeImage);
}

}  // namespace

TEST(TileGrid, TilesAreKnownByTypeBottomRowFirst) {
  TileGrid grid = mapOf(json::array({layer("ground", {"#.", "~#"})}));
  EXPECT_EQ(grid.width(), 2);
  EXPECT_EQ(grid.at(0, 0), "lava");  // bottom-left
  EXPECT_EQ(grid.at(1, 1), "grass");
  EXPECT_TRUE(grid.solid(1, 0));
  EXPECT_TRUE(grid.is(0, 0, "deadly"));
  EXPECT_FALSE(grid.solid(0, 0));
  EXPECT_EQ(grid.positionsOf("ground"), (std::vector<glm::ivec2>{{1, 0}, {0, 1}}));
  EXPECT_TRUE(grid.set(0, 0, "ground"));
  EXPECT_TRUE(grid.solid(0, 0));
  EXPECT_FALSE(grid.set(0, 0, "nothing"));  // no such type
  EXPECT_FALSE(grid.set(9, 9, "ground"));   // outside
}

TEST(TileGrid, SheetTilesCutByGridAndAnimate) {
  TileGrid grid = mapOf(json::array({layer("ground", {".~"})}));
  const auto [set, id] = grid.resolve(grid.gidAt(grid.layers()[0], 1, 0));
  ASSERT_NE(set, nullptr);
  EXPECT_EQ(set->tile(id)->image.texture.id, std::string("assets/img/tiles.png").size());
  EXPECT_EQ(set->tile(id)->image.texRect, glm::vec4(32, 0, 16, 16));
  EXPECT_EQ(set->frame(id, 0.2f)->image.texRect.x, 32);
  EXPECT_EQ(set->frame(id, 0.7f)->image.texRect.x, 48);
  EXPECT_EQ(set->frame(id, 1.2f)->image.texRect.x, 32);  // loops
}

TEST(TileGrid, LayersStackAndSetTargetsTheTopmostTile) {
  TileGrid grid = mapOf(json::array({layer("floor", {"..", ".."}), layer("walls", {"# ", "  "})}));
  EXPECT_EQ(grid.at(0, 1), "ground");  // the wall over the grass
  EXPECT_EQ(grid.at(1, 1), "grass");
  EXPECT_TRUE(grid.solid(0, 1));
  grid.set(0, 1, "");  // clears the wall, revealing the floor
  EXPECT_EQ(grid.at(0, 1), "grass");
  grid.set(1, 0, "ground", "walls");
  EXPECT_EQ(grid.gidAt(grid.layers()[1], 1, 0), 1u);
  // The topmost tile setting a property decides it: lava (no "solid") keeps the wall, a bridge doesn't.
  TileGrid over = mapOf(json::array({layer("floor", {"##"}), layer("top", {"~="})}));
  EXPECT_TRUE(over.solid(0, 0));
  EXPECT_TRUE(over.is(0, 0, "deadly"));
  EXPECT_FALSE(over.solid(1, 0));
  EXPECT_TRUE(grid.showLayer("walls", false));
  EXPECT_FALSE(grid.layers()[1].visible);
}

TEST(TileGrid, FlipFlagsDontChangeTheTile) {
  json flipped = layer("ground", {"#"});
  flipped["data"][0] = 1u | gid::FlipH | gid::FlipD;
  TileGrid grid = mapOf(json::array({flipped}));
  EXPECT_EQ(grid.at(0, 0), "ground");
  EXPECT_TRUE(grid.solid(0, 0));
}

TEST(TileGrid, Base64DataAndGroupLayersInherit) {
  // gids 1, 2 as little-endian uint32: AQAAAAIAAAA=
  json encoded = {{"type", "tilelayer"}, {"name", "b64"}, {"width", 2}, {"height", 1}, {"encoding", "base64"},
                  {"data", "AQAAAAIAAAA="}, {"offsetx", 4}};
  json group = {{"type", "group"}, {"name", "g"}, {"opacity", 0.5}, {"offsetx", 2}, {"offsety", 3},
                {"layers", json::array({encoded})}};
  TileGrid grid = mapOf(json::array({layer("base", {"  "}), group}), {{"width", 2}, {"height", 1}});
  ASSERT_EQ(grid.layers().size(), 2u);
  EXPECT_EQ(grid.at(0, 0), "ground");
  EXPECT_EQ(grid.at(1, 0), "grass");
  EXPECT_FLOAT_EQ(grid.layers()[1].opacity, 0.5f);
  EXPECT_EQ(grid.layers()[1].offset, glm::vec2(6, -3));  // y up
  EXPECT_GT(grid.layers()[1].z, grid.layers()[0].z);
}

TEST(TileGrid, ObjectsFlipToYUpAndOutsideIsAType) {
  json objects = {{"type", "objectgroup"}, {"name", "spawns"}, {"objects", json::array({
      {{"id", 1}, {"name", "start"}, {"type", "spawn"}, {"x", 8}, {"y", 4}, {"width", 16}, {"height", 8},
       {"properties", json::array({{{"name", "facing"}, {"type", "string"}, {"value", "left"}}})}},
      {{"id", 2}, {"gid", 2}, {"x", 0}, {"y", 32}, {"width", 16}, {"height", 16}}})}};
  json props = {{"properties", json::array({{{"name", "outside"}, {"type", "string"}, {"value", "ground"}},
                                            {{"name", "outsideTop"}, {"type", "string"}, {"value", ""}}})}};
  TileGrid grid = mapOf(json::array({layer("ground", {"..", ".."}), objects}), props);
  ASSERT_EQ(grid.objects().size(), 2u);
  const MapObject& start = grid.objects()[0];
  EXPECT_EQ(start.position, glm::vec2(8, 32 - 4 - 8));  // the top at y = 4 down: the bottom at 20 up
  EXPECT_EQ(start.properties["facing"], "left");
  EXPECT_EQ(start.layer, "spawns");
  EXPECT_EQ(grid.objects()[1].position, glm::vec2(0, 0));  // a tile object's y is its bottom
  EXPECT_EQ(grid.at(-1, 0), "ground");
  EXPECT_TRUE(grid.solid(5, 0));
  EXPECT_FALSE(grid.solid(0, 9));  // outsideTop overrides
}

TEST(TileGrid, ProblemsAreReported) {
  std::vector<std::string> errors;
  json map = {{"width", 1}, {"height", 1}, {"infinite", true},
              {"layers", json::array({{{"type", "imagelayer"}, {"name", "sky"}, {"image", "missing.png"}}})},
              {"tilesets", json::array({{{"firstgid", 1}, {"source", "gone.tsj"}}})}};
  TileGrid::parse(map, "m.tmj", [](const std::string&) { return nullptr; }, fakeImage,
                  [&](const std::string& e) { errors.push_back(e); });
  EXPECT_EQ(errors.size(), 3u);
}

TEST(TileGrid, BoxesStopFlushAndReportTheTile) {
  // A floor with a wall at x = 3.
  TileGrid grid = mapOf(json::array({layer("ground", {"...#", "...#", "####"})}));
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
  TileGrid grid = mapOf(json::array({layer("ground", {"...", "#.#", "..."})}));
  auto up = grid.move({27, 8}, {6, 6}, {0, 4}, 6);
  EXPECT_EQ(up.hit.y, 1);
  EXPECT_LT(up.position.x, 27.0f);
  auto stuck = grid.move({27, 8}, {6, 6}, {0, 4}, 0);
  EXPECT_FLOAT_EQ(stuck.position.x, 27.0f);
}

TEST(TileGrid, NonSquareTilesMoveOnTheirOwnAxes) {
  // 32 wide, 8 tall: a floor row under an open row.
  TileGrid grid = mapOf(json::array({layer("ground", {"..", "##"})}), {{"tilewidth", 32}, {"tileheight", 8}});
  EXPECT_EQ(grid.pixelSize(), glm::vec2(64, 16));
  EXPECT_EQ(grid.tileOf({40, 9}), glm::ivec2(1, 1));
  auto fall = grid.move({16, 14}, {4, 2}, {0, -10});
  EXPECT_NEAR(fall.position.y, 10.01f, 0.001f);
}

// A NaN move stays put, and a box far bigger than the map is checked in bounded time.
TEST(TileGrid, DegenerateMovesAreSafe) {
  TileGrid grid = mapOf(json::array({layer("ground", {"...", "..."})}));
  auto nan = grid.move({8, 8}, {4, 4}, {std::nanf(""), 1});
  EXPECT_EQ(nan.position, glm::vec2(8, 8));
  auto huge = grid.move({8, 8}, {1e9f, 1e9f}, {0, 1});
  EXPECT_FLOAT_EQ(huge.position.y, 9.0f);
}

// Ground drawn on an object layer (classes "ground" and "platform"): polylines,
// polygons and rectangles become terrain lines, flipped to y up like objects.
TEST(TileGrid, DrawnGroundIsTerrain) {
  json objects = {{"type", "objectgroup"}, {"name", "ground"}, {"objects", json::array({
      {{"id", 1}, {"type", "ground"}, {"x", 0}, {"y", 32},
       {"polyline", json::array({{{"x", 0}, {"y", 0}}, {{"x", 16}, {"y", -8}}, {{"x", 32}, {"y", 0}}})}},
      {{"id", 2}, {"class", "platform"}, {"x", 0}, {"y", 8}, {"width", 16}, {"height", 4}},
      {{"id", 3}, {"type", "ground"}, {"x", 16}, {"y", 32}, {"visible", false},
       {"polygon", json::array({{{"x", 0}, {"y", 0}}, {{"x", 8}, {"y", 0}}, {{"x", 8}, {"y", -8}}})}},
      {{"id", 4}, {"type", "door"}, {"x", 0}, {"y", 0}, {"width", 8}, {"height", 8}}})}};
  TileGrid grid = mapOf(json::array({layer("tiles", {"..", ".."}), objects}));
  EXPECT_EQ(grid.objects()[0].points, (std::vector<glm::vec2>{{0, 0}, {16, 8}, {32, 0}}));  // map 32 px tall
  EXPECT_EQ(grid.objects()[0].position, glm::vec2(0, 0));
  EXPECT_TRUE(grid.objects()[2].closed);
  struct Line {
    glm::vec2 a, b;
    bool oneWay;
    bool operator==(const Line&) const = default;
  };
  std::vector<Line> lines;
  grid.forEachTerrainLine([&](glm::vec2 a, glm::vec2 b, bool oneWay) { lines.push_back({a, b, oneWay}); });
  ASSERT_EQ(lines.size(), 2u + 4u + 3u);  // the door isn't ground
  EXPECT_EQ(lines[0], (Line{{0, 0}, {16, 8}, false}));
  EXPECT_EQ(lines[2], (Line{{0, 20}, {16, 20}, true}));  // the platform rectangle's bottom edge, y up
  EXPECT_EQ(lines[8], (Line{{24, 8}, {16, 0}, false}));  // the polygon closes
}
