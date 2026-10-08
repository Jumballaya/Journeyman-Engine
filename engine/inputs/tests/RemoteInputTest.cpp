#include <gtest/gtest.h>

#include "RemoteInput.hpp"

namespace {
InputSnapshot snapshot(bool fire, bool space) {
  InputSnapshot s;
  s.actions.push_back({"fire", fire, fire ? 1.0f : 0.0f});
  s.setKey(inputs::Key::Space, space);
  return s;
}
}  // namespace

TEST(RemoteInput, PressAndReleaseLastOneFrame) {
  RemoteInput in;
  in.tick(1.0f / 60);
  in.apply(snapshot(true, true));
  EXPECT_TRUE(in.down("fire"));
  EXPECT_TRUE(in.pressed("fire"));
  EXPECT_TRUE(in.keys().keyIsPressed(inputs::Key::Space));
  in.tick(1.0f / 60);
  EXPECT_TRUE(in.down("fire"));
  EXPECT_FALSE(in.pressed("fire"));
  in.apply(snapshot(false, false));
  EXPECT_FALSE(in.down("fire"));
  EXPECT_TRUE(in.released("fire"));
  EXPECT_TRUE(in.keys().keyIsReleased(inputs::Key::Space));
}

TEST(RemoteInput, ATapInsideOneFrameStillPresses) {
  RemoteInput in;
  in.tick(1.0f / 60);
  in.apply(snapshot(true, true));
  in.apply(snapshot(false, false));
  EXPECT_TRUE(in.pressed("fire"));
  EXPECT_TRUE(in.released("fire"));
  EXPECT_FALSE(in.down("fire"));
}

TEST(RemoteInput, RepeatsAfterTheDelay) {
  constexpr float kStep = 1.0f / 32;  // exact in binary, under the 1/20 s clamp
  RemoteInput in;
  in.tick(kStep);
  in.apply(snapshot(true, false));
  EXPECT_TRUE(in.repeated("fire", 0.25f, 0.125f));  // the press
  int repeats = 0;
  for (int i = 0; i < 16; ++i) {  // held 1/32 .. 1/2 s
    in.tick(kStep);
    if (in.repeated("fire", 0.25f, 0.125f)) ++repeats;
  }
  EXPECT_EQ(repeats, 3);  // at 0.25, 0.375, 0.5
}

TEST(RemoteInput, UnknownActionsAreUp) {
  RemoteInput in;
  EXPECT_FALSE(in.down("jump"));
  EXPECT_FLOAT_EQ(in.value("jump"), 0.0f);
}
