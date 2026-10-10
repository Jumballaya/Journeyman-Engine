#include <gtest/gtest.h>

#include "InputActions.hpp"

#include <vector>

TEST(InputActions, ParsesKeysAndGamepadControls) {
  auto space = inputs::parseControl("Space");
  ASSERT_TRUE(space.has_value());
  EXPECT_EQ(std::get<inputs::Key>(*space), inputs::Key::Space);

  auto stick = inputs::parseControl("Gamepad.LeftStickUp");
  ASSERT_TRUE(stick.has_value());
  EXPECT_EQ(std::get<inputs::Pad>(*stick), inputs::Pad::LeftStickUp);

  EXPECT_FALSE(inputs::parseControl("Gamepad.Nope").has_value());
  EXPECT_FALSE(inputs::parseControl("NotAKey").has_value());
  EXPECT_EQ(inputs::keyName(inputs::Key::ArrowLeft), "ArrowLeft");
}

TEST(InputActions, ActionsFollowBoundKeys) {
  InputsManager keys;
  InputActions actions;
  actions.loadBindings(nlohmann::json::parse(R"({"actions":{"fire":["Space","Z","Bogus"]}})"), "test");

  EXPECT_FALSE(actions.down("fire", keys));
  keys.registerKeyDown(inputs::Key::Z);
  EXPECT_TRUE(actions.down("fire", keys));
  EXPECT_TRUE(actions.justPressed("fire", keys));
  EXPECT_FLOAT_EQ(actions.value("fire", keys), 1.0f);

  keys.tick(0.016f);  // clears edges
  EXPECT_TRUE(actions.down("fire", keys));
  EXPECT_FALSE(actions.justPressed("fire", keys));

  keys.registerKeyUp(inputs::Key::Z);
  EXPECT_TRUE(actions.justReleased("fire", keys));
  EXPECT_FALSE(actions.down("unbound-action", keys));
}

TEST(InputActions, RuntimeBindAndUnbind) {
  InputsManager keys;
  InputActions actions;
  EXPECT_TRUE(actions.bind("pause", "Escape"));
  EXPECT_FALSE(actions.bind("pause", "Gamepad.Nope"));
  keys.registerKeyDown(inputs::Key::Escape);
  EXPECT_TRUE(actions.justPressed("pause", keys));
  actions.unbind("pause");
  EXPECT_FALSE(actions.justPressed("pause", keys));
}

TEST(InputActions, EitherSideModifierAliases) {
  InputsManager keys;
  InputActions actions;
  actions.loadBindings(nlohmann::json::parse(R"({"actions":{"hold":["Shift"]}})"), "test");
  keys.registerKeyDown(inputs::Key::RightShift);
  EXPECT_TRUE(actions.justPressed("hold", keys));
  EXPECT_TRUE(actions.bind("menu", "Ctrl"));
  keys.registerKeyDown(inputs::Key::LeftCtrl);
  EXPECT_TRUE(actions.down("menu", keys));
  EXPECT_EQ(inputs::keyName(inputs::Key::LeftAlt), "LeftAlt");
}

TEST(InputActions, RepeatFiresOnPressThenAfterDelayEveryInterval) {
  InputsManager keys;
  InputActions actions;
  actions.bind("left", "ArrowLeft");
  keys.registerKeyDown(inputs::Key::ArrowLeft);
  std::vector<int> frames;
  for (int frame = 0; frame < 30; ++frame) {  // frames of 1/64 s (exact in binary)
    if (actions.repeated("left", keys, 0.125f, 0.0625f)) frames.push_back(frame);
    keys.tick(1.0f / 64.0f);
  }
  // Press at frame 0, first repeat after 8 frames, then every 4.
  EXPECT_EQ(frames, (std::vector<int>{0, 8, 12, 16, 20, 24, 28}));
}

TEST(InputActions, MouseButtonsBindLikeKeysAndWheelLastsAFrame) {
  InputsManager keys;
  InputActions actions;
  actions.loadBindings(nlohmann::json::parse(R"({"actions":{"confirm":["Enter","MouseLeft"]}})"), "test");
  EXPECT_EQ(inputs::keyName(inputs::Key::MouseRight), "MouseRight");

  keys.registerKeyDown(inputs::Key::MouseLeft);
  EXPECT_TRUE(actions.justPressed("confirm", keys));

  // Scroll gathered during a frame is what scripts see over the next one, then it's gone.
  keys.registerWheel(0.0f, 1.5f);
  keys.registerWheel(0.0f, 0.5f);
  keys.tick(0.016f);
  EXPECT_FLOAT_EQ(keys.wheel().y, 2.0f);
  keys.tick(0.016f);
  EXPECT_FLOAT_EQ(keys.wheel().y, 0.0f);
}
