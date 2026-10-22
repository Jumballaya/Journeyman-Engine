#include <gtest/gtest.h>

#include <fstream>

#include "../../core/tests/assets/TempDir.hpp"
#include "Engine.hpp"

// An inspection's draw list is the frame the scene starts with, each item its
// entity's: what a tool shows is what the engine draws (a "#ff0000" sprite red).
TEST(InspectDraw, EachItemIsItsEntitys) {
  TempDir game;
  game.writeFile(".jm.json", R"({"name": "D", "entryScene": "main.scene.json", "scenes": ["main.scene.json"]})");
  game.writeFile("main.scene.json", R"({"name": "main", "entities": [
    {"name": "Red", "components": {"TransformComponent": {"position": [5, 6, 1], "scale": [10, 4]}, "SpriteComponent": {"color": "#ff0000"}}},
    {"name": "Bare", "components": {"TransformComponent": {}}}]})");
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
  ASSERT_EQ(seen["entities"].size(), 2u);
  ASSERT_EQ(seen["draw"]["world"].size(), 1u);
  const auto& red = seen["draw"]["world"][0];
  EXPECT_EQ(red["entity"], seen["entities"][0]["id"]);
  EXPECT_EQ(red["color"], (nlohmann::json{1, 0, 0, 1}));
  EXPECT_EQ(red["center"], (nlohmann::json{5, 6}));
  EXPECT_EQ(red["image"], "white");
  EXPECT_TRUE(seen["camera"].contains("zoom"));
}
