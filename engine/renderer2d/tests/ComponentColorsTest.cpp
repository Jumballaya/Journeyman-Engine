#include <gtest/gtest.h>

#include "../../core/tests/assets/TempDir.hpp"
#include "Engine.hpp"
#include "Lights.hpp"
#include "SpriteComponent.hpp"

// A scene's colors are read as jm's schema writes them: #hex and [r, g, b] too.
TEST(ComponentColors, HexAndThreeNumbersAreTheirColors) {
  TempDir game;
  game.writeFile(".jm.json", R"({"name": "C", "entryScene": "main.scene.json", "scenes": ["main.scene.json"]})");
  game.writeFile("main.scene.json", R"({"name": "main", "entities": [
    {"name": "Hex", "components": {"TransformComponent": {}, "SpriteComponent": {"color": "#ff0000", "shadow": {"color": "#00f8"}},
                                   "PointLightComponent": {"color": "#00ff00"}}},
    {"name": "Three", "components": {"TransformComponent": {}, "SpriteComponent": {"color": [0, 0, 1], "texRect": [0, 0, 0.5, 0.5]},
                                     "PointLightComponent": {"color": [1, 1, 0]}}}]})");
  EngineOptions options;
  options.dev = DevOptions{};
  options.dev.renderer = "none";
  Engine engine(game.path(), ".jm.json", options);
  engine.initialize();
  World& world = engine.getWorld();
  int seen = 0;
  for (auto [entity, sprite] : world.view<SpriteComponent>()) {
    const auto* light = world.getComponent<PointLightComponent>(entity);
    ASSERT_TRUE(light);
    if (world.hasTag(entity, "Hex")) {
      EXPECT_EQ(sprite->color, glm::vec4(1, 0, 0, 1));
      EXPECT_EQ(sprite->shadow.color, glm::vec4(0, 0, 1, 136 / 255.0f));
      EXPECT_EQ(light->color, glm::vec3(0, 1, 0));
    } else {
      EXPECT_EQ(sprite->color, glm::vec4(0, 0, 1, 1));
      EXPECT_EQ(sprite->texRect, glm::vec4(0, 0, 0.5f, 0.5f));
      EXPECT_EQ(light->color, glm::vec3(1, 1, 0));
    }
    ++seen;
  }
  EXPECT_EQ(seen, 2);
}
