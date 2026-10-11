#include <gtest/gtest.h>

#include <cmath>
#include <nlohmann/json.hpp>

#include "../../ui/DebugDraw.hpp"
#include "Renderer2D.hpp"

using Shape = DebugDraw::Shape;

// A frame's drawing lasts until the next frame starts; a timed one its seconds of game time.
TEST(DebugDraw, ItemsLastAFrameOrTheirSeconds) {
  DebugDraw debug;
  debug.add(Shape::Line, false, {0, 0}, {10, 0}, 0xff0000, 0.0f, false);
  debug.add(Shape::Circle, false, {5, 5}, {3, 3}, 0x00ff00, 0.06f, false);
  debug.expire(1.0f / 60.0f, false);
  ASSERT_EQ(debug.items().size(), 1u);
  EXPECT_EQ(debug.items()[0].shape, Shape::Circle);
  debug.expire(1.0f / 60.0f, false);
  debug.expire(1.0f / 60.0f, false);
  EXPECT_EQ(debug.items().size(), 1u);
  debug.expire(1.0f / 60.0f, false);
  EXPECT_TRUE(debug.items().empty());
}

// Paused, what stopped scripts drew stays on screen; a pause menu's (runWhenPaused) is redrawn each frame.
TEST(DebugDraw, APauseKeepsTheGamesDrawing) {
  DebugDraw debug;
  debug.add(Shape::Rect, false, {0, 0}, {4, 4}, 0xffffff, 0.0f, false);
  debug.add(Shape::Text, true, {8, 8}, {}, 0xffffff, 0.0f, true, "menu");
  debug.expire(0.0f, true);
  ASSERT_EQ(debug.items().size(), 1u);
  EXPECT_EQ(debug.items()[0].shape, Shape::Rect);
  debug.expire(1.0f / 60.0f, false);
  EXPECT_TRUE(debug.items().empty());
}

TEST(DebugDraw, NonsenseIsIgnored) {
  DebugDraw debug;
  debug.add(static_cast<Shape>(9), false, {0, 0}, {1, 1}, 0, 0.0f, false);
  debug.add(Shape::Line, false, {NAN, 0}, {1, 1}, 0, 0.0f, false);
  debug.add(Shape::Line, false, {0, 0}, {1, 1}, 0, INFINITY, false);
  EXPECT_TRUE(debug.items().empty());
}

// Shapes are drawn as screen lines (over the UI); world ones placed through the camera.
TEST(DebugDraw, ShapesDrawAsLinesOverEverything) {
  Renderer2D renderer;
  ASSERT_TRUE(renderer.initialize(320, 180, RenderSettings{320, 180}, /*gpu=*/false));
  renderer.camera().setPosition({100.0f, 0.0f});
  DebugDraw debug;
  debug.add(Shape::Rect, false, {0, 0}, {4, 4}, 0xff8000, 0.0f, false);
  debug.add(Shape::Circle, false, {0, 0}, {2, 2}, 0xff8000, 0.0f, false);
  debug.add(Shape::Line, false, {100, 10}, {110, 10}, 0xff8000, 0.0f, false);
  debug.add(Shape::Line, true, {0, 0}, {10, 0}, 0xff8000, 0.0f, false);
  debug.drawShapes(renderer, false);
  renderer.endFrame();
  const auto& drawn = renderer.drawnScreen();
  ASSERT_EQ(drawn.size(), 4u + 24u + 1u);
  EXPECT_TRUE(renderer.drawnWorld().empty());
  const glm::vec4 orange(1.0f, 128.0f / 255.0f, 0.0f, 1.0f);
  EXPECT_EQ(drawn.back().instance.color, orange);
  const glm::vec3 center(drawn.back().instance.transform[3]);
  EXPECT_FLOAT_EQ(center.x, 165.0f);  // 5 right of the camera's center, 10 up: y down on screen
  EXPECT_FLOAT_EQ(center.y, 80.0f);
  renderer.shutdown();
}

// The state dump says what's drawn, so a tool can read it without an image.
TEST(DebugDraw, TheStateSaysWhatsDrawn) {
  DebugDraw debug;
  debug.add(Shape::Line, false, {1, 2}, {3, 4}, 0xff0000, 0.0f, false);
  debug.add(Shape::Text, true, {8, 9}, {}, 0x0000ff, 2.0f, false, "hp 3");
  const nlohmann::json state = debug.describe();
  ASSERT_EQ(state.size(), 2u);
  EXPECT_EQ(state[0]["shape"], "line");
  EXPECT_EQ(state[0]["from"], nlohmann::json({1.0, 2.0}));
  EXPECT_EQ(state[0]["to"], nlohmann::json({3.0, 4.0}));
  EXPECT_EQ(state[1]["space"], "screen");
  EXPECT_EQ(state[1]["text"], "hp 3");
  EXPECT_EQ(state[1]["seconds"], 2.0);
}
