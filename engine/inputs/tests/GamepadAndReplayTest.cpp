#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

#include "InputActions.hpp"
#include "Replay.hpp"

namespace {

using Reading = InputActions::GamepadReading;

InputActions withBindings(const char* json) {
  InputActions actions;
  actions.loadBindings(nlohmann::json::parse(json), "test");
  return actions;
}

Reading pressing(inputs::Pad button) {
  Reading r;
  r.buttons[static_cast<size_t>(button)] = true;
  return r;
}

}  // namespace

TEST(Gamepad, ButtonsPressHoldAndRelease) {
  InputsManager keys;
  InputActions actions = withBindings(R"({"actions":{"fire":["Space","Gamepad.A"]}})");
  actions.applyGamepads(std::vector<Reading>{pressing(inputs::Pad::A)}, 0.016f);
  EXPECT_TRUE(actions.gamepadConnected());
  EXPECT_TRUE(actions.justPressed("fire", keys));
  EXPECT_TRUE(actions.down("fire", keys));

  actions.applyGamepads(std::vector<Reading>{pressing(inputs::Pad::A)}, 0.016f);
  EXPECT_FALSE(actions.justPressed("fire", keys));  // an edge lasts one poll
  EXPECT_TRUE(actions.down("fire", keys));

  actions.applyGamepads(std::vector<Reading>{Reading{}}, 0.016f);
  EXPECT_TRUE(actions.justReleased("fire", keys));
  EXPECT_FALSE(actions.down("fire", keys));
}

// Sticks: nothing inside the deadzone, then 0..1, and down from halfway.
TEST(Gamepad, SticksHaveADeadzoneAndScale) {
  InputsManager keys;
  InputActions actions = withBindings(R"({"actions":{"left":["Gamepad.LeftStickLeft"],"right":["Gamepad.LeftStickRight"]}})");
  Reading r;
  r.axes[0] = -0.2f;  // inside the 0.25 deadzone
  actions.applyGamepads(std::vector<Reading>{r}, 0.016f);
  EXPECT_FLOAT_EQ(actions.value("left", keys), 0.0f);

  r.axes[0] = -1.0f;
  actions.applyGamepads(std::vector<Reading>{r}, 0.016f);
  EXPECT_FLOAT_EQ(actions.value("left", keys), 1.0f);
  EXPECT_TRUE(actions.down("left", keys));
  EXPECT_FLOAT_EQ(actions.value("right", keys), 0.0f);

  r.axes[0] = 0.5f;  // (0.5 - 0.25) / 0.75 = 1/3: some value, not yet down
  actions.applyGamepads(std::vector<Reading>{r}, 0.016f);
  EXPECT_NEAR(actions.value("right", keys), 1.0f / 3.0f, 1e-5f);
  EXPECT_FALSE(actions.down("right", keys));
}

TEST(Gamepad, TriggersRestAtMinusOne) {
  InputsManager keys;
  InputActions actions = withBindings(R"({"actions":{"throttle":["Gamepad.RightTrigger"]}})");
  actions.applyGamepads(std::vector<Reading>{Reading{}}, 0.016f);  // resting
  EXPECT_FLOAT_EQ(actions.value("throttle", keys), 0.0f);
  Reading r;
  r.axes[5] = 1.0f;
  actions.applyGamepads(std::vector<Reading>{r}, 0.016f);
  EXPECT_FLOAT_EQ(actions.value("throttle", keys), 1.0f);
}

// Two pads act as one: the strongest of each control wins.
TEST(Gamepad, PadsMerge) {
  InputsManager keys;
  InputActions actions = withBindings(R"({"actions":{"fire":["Gamepad.A"],"jump":["Gamepad.B"]}})");
  actions.applyGamepads(std::vector<Reading>{pressing(inputs::Pad::A), pressing(inputs::Pad::B)}, 0.016f);
  EXPECT_TRUE(actions.down("fire", keys));
  EXPECT_TRUE(actions.down("jump", keys));
}

TEST(Gamepad, NoPadsMeansDisconnectedAndNothingDown) {
  InputsManager keys;
  InputActions actions = withBindings(R"({"actions":{"fire":["Gamepad.A"]}})");
  actions.applyGamepads(std::vector<Reading>{pressing(inputs::Pad::A)}, 0.016f);
  actions.applyGamepads({}, 0.016f);  // unplugged while held
  EXPECT_FALSE(actions.gamepadConnected());
  EXPECT_TRUE(actions.justReleased("fire", keys));
}

TEST(Replay, ParsesEventsInFrameOrder) {
  std::istringstream file(R"(# a comment line
120 down Space      # hold fire from frame 120
40 down Enter
42 up Enter

40 down ArrowLeft
)");
  std::vector<std::string> skipped;
  const auto events = inputs::parseReplay(file, &skipped);
  ASSERT_EQ(events.size(), 4u);
  EXPECT_EQ(events[0].frame, 40u);
  EXPECT_EQ(events[0].key, inputs::Key::Enter);  // same frame: file order kept
  EXPECT_EQ(events[1].key, inputs::Key::ArrowLeft);
  EXPECT_EQ(events[2].frame, 42u);
  EXPECT_FALSE(events[2].down);
  EXPECT_EQ(events[3].frame, 120u);
  EXPECT_TRUE(skipped.empty());  // comments and blank lines aren't mistakes
}

TEST(Replay, BadLinesAreSkippedAndReported) {
  std::istringstream file("10 down Space\n10 press Space\n11 down NotAKey\n12 down Gamepad.A\nnonsense\n13 up Space\n");
  std::vector<std::string> skipped;
  const auto events = inputs::parseReplay(file, &skipped);
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(skipped.size(), 4u);  // a bad action, a bad key, a pad control (keys only), nonsense
}
