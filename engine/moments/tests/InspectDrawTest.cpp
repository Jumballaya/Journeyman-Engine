#include <gtest/gtest.h>

#include <stb_image_write.h>

#include <fstream>

#include "../../core/tests/assets/TempDir.hpp"
#include "Engine.hpp"

// An inspection's draw list is the frame the scene starts with, each item its
// entity's (a sign's glyphs too), through the camera the scene shows: what a
// tool shows is what the engine draws (a "#ff0000" sprite red).
TEST(InspectDraw, EachItemIsItsEntitys) {
  TempDir game;
  game.writeFile(".jm.json", R"({"name": "D", "entryScene": "main.scene.json", "scenes": ["main.scene.json"]})");
  game.writeFile("main.scene.json", R"({"name": "main", "entities": [
    {"name": "Red", "components": {"TransformComponent": {"position": [5, 6, 1], "scale": [10, 4]}, "SpriteComponent": {"color": "#ff0000"}}},
    {"name": "Bare", "components": {"TransformComponent": {}}},
    {"name": "Eye", "components": {"TransformComponent": {"position": [500, 300, 0]}, "CameraComponent": {"zoom": 2}}},
    {"name": "Sign", "components": {"TransformComponent": {"position": [500, 300, 0]}, "TextComponent": {"text": "Hi"}}}]})");
  EngineOptions options;
  options.dev = DevOptions{};
  options.dev.renderer = "none";
  options.dev.inspectOut = game.path() / "seen.json";
  options.dev.saveDir = game.path() / "save";
  {
    Engine engine(game.path(), ".jm.json", options);
    engine.initialize();
    engine.run();
  }
  std::ifstream in(options.dev.inspectOut);
  const auto seen = nlohmann::json::parse(in);
  ASSERT_EQ(seen["entities"].size(), 4u);
  ASSERT_EQ(seen["draw"]["world"].size(), 1u);
  const auto& red = seen["draw"]["world"][0];
  EXPECT_EQ(red["entity"], seen["entities"][0]["id"]);
  EXPECT_EQ(red["color"], (nlohmann::json{1, 0, 0, 1}));
  EXPECT_EQ(red["center"], (nlohmann::json{5, 6}));
  EXPECT_EQ(red["image"], "white");
  EXPECT_EQ(seen["camera"]["x"], 500);
  EXPECT_EQ(seen["camera"]["zoom"], 2);
  const auto& glyphs = seen["draw"]["screen"];
  ASSERT_FALSE(glyphs.empty());
  for (const auto& g : glyphs) EXPECT_EQ(g["entity"], seen["entities"][3]["id"]);
}

// Tile maps scroll by the camera the frame is drawn with: a half-speed
// parallax layer sits halfway to a camera at x=1000, not where it'd be at 0.
TEST(InspectDraw, ParallaxIsByTheShownCamera) {
  TempDir game;
  const unsigned char pixel[4] = {255, 255, 255, 255};
  stbi_write_png((game.path() / "far.png").string().c_str(), 1, 1, 4, pixel, 4);
  game.writeFile("level.tmj", R"({"width": 4, "height": 2, "tilewidth": 16, "tileheight": 16, "layers": [
      {"type": "imagelayer", "image": "far.png", "parallaxx": 0.5, "parallaxy": 1}]})");
  game.writeFile(".jm.json", R"({"name": "P", "entryScene": "main.scene.json", "scenes": ["main.scene.json"],
                                 "assets": ["far.png", "level.tmj"]})");
  game.writeFile("main.scene.json", R"({"name": "main", "entities": [
    {"name": "Map", "components": {"TransformComponent": {}, "TileMapComponent": {"map": "level.tmj"}}},
    {"name": "Eye", "components": {"TransformComponent": {"position": [1000, 0, 0]}, "CameraComponent": {}}}]})");
  EngineOptions options;
  options.dev = DevOptions{};
  options.dev.renderer = "none";
  options.dev.inspectOut = game.path() / "seen.json";
  options.dev.saveDir = game.path() / "save";
  {
    Engine engine(game.path(), ".jm.json", options);
    engine.initialize();
    engine.run();
  }
  std::ifstream in(options.dev.inspectOut);
  const auto seen = nlohmann::json::parse(in);
  ASSERT_EQ(seen["draw"]["world"].size(), 1u) << seen.dump();
  EXPECT_NEAR(seen["draw"]["world"][0]["center"][0].get<float>(), 500.0f, 1.0f);
}
