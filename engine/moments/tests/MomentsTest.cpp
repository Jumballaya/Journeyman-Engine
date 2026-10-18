#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include "../../core/tests/assets/TempDir.hpp"
#include "Engine.hpp"

// A held run's picks through the driver, in a 640x400 frame showing a
// 320x180 game (scale 2, 20 px bars above and below), its camera at (64, 32)
// zoomed 2x: logical (x, y) is the world's (64 + (x - 160) / 2, 32 + (90 - y) / 2).
// The map: 8x4 cells of 16, at the origin; ground along the bottom row and at (3, 1).
TEST(Moments, PointsAndAreasSayWhatsThere) {
  TempDir game;
  game.writeFile("tiles.tsj", R"({"type": "tileset", "tilewidth": 16, "tileheight": 16, "tilecount": 1, "columns": 1,
                                  "tiles": [{"id": 0, "type": "ground"}]})");
  game.writeFile("level.tmj", R"({"width": 8, "height": 4, "tilewidth": 16, "tileheight": 16,
      "tilesets": [{"firstgid": 1, "source": "tiles.tsj"}],
      "layers": [{"type": "tilelayer", "name": "ground", "width": 8, "height": 4,
                  "data": [0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0, 0,0,0,1,0,0,0,0, 1,1,1,1,1,1,1,1]}]})");
  game.writeFile(".jm.json", R"({"name": "T", "entryScene": "main.scene.json", "scenes": ["main.scene.json"],
      "assets": ["level.tmj", "tiles.tsj"],
      "config": {"window": {"width": 640, "height": 400}, "renderer": {"logicalWidth": 320, "logicalHeight": 180}}})");
  game.writeFile("main.scene.json", R"({"name": "main", "entities": [
      {"name": "Map", "components": {"TransformComponent": {"position": [0, 0, 0]}, "TileMapComponent": {"map": "level.tmj"}}},
      {"name": "Eye", "components": {"TransformComponent": {"position": [64, 32, 0]}, "CameraComponent": {"zoom": 2, "smoothing": 0}}},
      {"name": "Coin", "components": {"TransformComponent": {"position": [100, 40, 1], "scale": [4, 4]}, "SpriteComponent": {}}}]})");
  EngineOptions options;
  options.dev = DevOptions{};
  options.dev.renderer = "none";
  options.dev.drive = true;
  options.dev.recordDir = game.path() / "play";
  options.dev.saveDir = game.path() / "save";
  Engine engine(game.path(), ".jm.json", options);
  engine.initialize();
  // A point on (56, 24): ground (3, 1); the coin; an area from (40, 50) to (110, 10); shift
  // near the point takes it away, and on empty ground again puts it back.
  std::istringstream in("step 2\nfreeze\npick 144 106\npick 232 74 add\narea 112 54 252 134 add\n"
                        "pick 146 107 add\npick 144 106 add\nsave\n");
  std::ostringstream out;
  engine.agent().drive(in, out);
  std::vector<nlohmann::json> replies;
  for (std::istringstream lines(out.str()); !lines.eof();) {
    std::string line;
    if (std::getline(lines, line) && !line.empty()) replies.push_back(nlohmann::json::parse(line));
  }
  ASSERT_EQ(replies.size(), 9u) << out.str();  // "ready", then one per command
  EXPECT_EQ(replies[3]["picks"], nlohmann::json::parse(R"([{"kind": "point", "at": [56.0, 24.0]}])"));
  EXPECT_EQ(replies[4]["picks"][1]["kind"], "entity");
  EXPECT_EQ(replies[5]["picks"][2], nlohmann::json::parse(R"({"kind": "area", "area": [40.0, 10.0, 110.0, 50.0]})"));
  EXPECT_EQ(replies[6]["picks"].size(), 2u);
  EXPECT_EQ(replies[7]["picks"].size(), 3u);
  EXPECT_EQ(replies[8]["picks"], 3);
  engine.shutdown();

  std::ifstream meta(game.path() / "play" / "session.json");
  const nlohmann::json picks = nlohmann::json::parse(meta)["markers"][0]["picks"];
  ASSERT_EQ(picks.size(), 3u);
  const nlohmann::json& area = picks[1];
  // Its picture's rect: logical (112, 54)-(252, 134), in the frame (224, 128)-(504, 288).
  EXPECT_EQ(area["rect"], (nlohmann::json{0.35, 0.32, 0.4375, 0.4}));
  ASSERT_EQ(area["entities"].size(), 1u);
  EXPECT_EQ(area["entities"][0]["name"], "Coin");
  ASSERT_EQ(area["tiles"].size(), 1u);
  const nlohmann::json& tiles = area["tiles"][0];
  EXPECT_EQ(tiles["map"], "level.tmj");
  EXPECT_EQ(tiles["from"], (nlohmann::json{2, 0}));
  EXPECT_EQ(tiles["to"], (nlohmann::json{6, 3}));
  ASSERT_EQ(tiles["layers"].size(), 1u);
  EXPECT_EQ(tiles["layers"][0]["count"], 6);
  EXPECT_EQ(tiles["layers"][0]["types"], (nlohmann::json{{"ground", 6}}));
  const nlohmann::json& point = picks[2];
  EXPECT_EQ(point["kind"], "point");
  EXPECT_EQ(point["scene"], "main.scene.json");
  EXPECT_EQ(point["rect"], (nlohmann::json{0.45, 0.58, 0.0, 0.0}));
  EXPECT_TRUE(point["entities"].empty());
  EXPECT_EQ(point["tiles"][0]["layers"][0]["cells"], nlohmann::json::parse("[[3, 1, 1]]"));
}
