#include <gtest/gtest.h>

#include "InputActions.hpp"

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
  EXPECT_TRUE(actions.pressed("fire", keys));
  EXPECT_FLOAT_EQ(actions.value("fire", keys), 1.0f);

  keys.tick(0.016f);  // clears edges
  EXPECT_TRUE(actions.down("fire", keys));
  EXPECT_FALSE(actions.pressed("fire", keys));

  keys.registerKeyUp(inputs::Key::Z);
  EXPECT_TRUE(actions.released("fire", keys));
  EXPECT_FALSE(actions.down("unbound-action", keys));
}

TEST(InputActions, RuntimeBindAndUnbind) {
  InputsManager keys;
  InputActions actions;
  EXPECT_TRUE(actions.bind("pause", "Escape"));
  EXPECT_FALSE(actions.bind("pause", "Gamepad.Nope"));
  keys.registerKeyDown(inputs::Key::Escape);
  EXPECT_TRUE(actions.pressed("pause", keys));
  actions.unbind("pause");
  EXPECT_FALSE(actions.pressed("pause", keys));
}
